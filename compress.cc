#include <node.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <node_buffer.h>
#include <string>
#include "buffer_compat.h"

#ifdef  WITH_GZIP
#include <zlib.h>
#endif//WITH_GZIP

#ifdef  WITH_BZIP
#define BZ_NO_STDIO
#include <bzlib.h>
#undef BZ_NO_STDIO
#endif//WITH_BZIP

#define CHUNK 16384

#include <node_object_wrap.h>
#if NODE_VERSION_AT_LEAST(0, 11, 0)
// for node 0.12.x and later
#define FUNC(name) static void name(const FunctionCallbackInfo<Value>& args)
#define FUNC_START \
	Isolate* isolate = args.GetIsolate(); \
	HandleScope scope(isolate)
#define FUNC_INIT_START \
	Isolate* isolate = target->GetIsolate(); \
	HandleScope scope(isolate)

#define STRING_EMPTY String::Empty(isolate)
#define NEW_STRING(s) String::NewFromOneByte(isolate, (const uint8_t*)s)
#define NEW_STRING_CONST NEW_STRING

#define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromOneByte(isolate, (const uint8_t*)e))); return; }
#define RETURN_VAL(v) args.GetReturnValue().Set(v)
#define RETURN_THIS args.GetReturnValue().Set(args.This())
#define RETURN_UNDEF return;
#define RETURN_BUFFER RETURN_VAL
#define ISOLATE isolate,
#else
// for node 0.10.x
#define FUNC(name) static Handle<Value> name(const Arguments& args)
#define FUNC_START HandleScope scope
#define FUNC_INIT_START FUNC_START
#define STRING_EMPTY String::Empty()
#define NEW_STRING String::New
#define NEW_STRING_CONST String::NewSymbol
#define RETURN_ERROR(e) \
	return ThrowException(Exception::Error( \
		String::New(e)) \
	)
#define RETURN_VAL(v) return scope.Close(v)
#define RETURN_THIS return args.This()
#define RETURN_UNDEF RETURN_VAL( Undefined() );
#define RETURN_BUFFER(b) RETURN_VAL((b)->handle_)
#define ISOLATE
#endif

#if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__).ToLocalChecked()
#define BUFFERP_T Local<Object>
#else
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__)
#define BUFFERP_T Buffer*
#endif


#define THROW_IF_NOT(condition, text) if (!(condition)) { \
      RETURN_ERROR(text); \
    }
#define THROW_IF_NOT_A(condition,...) if (!(condition)) { \
   char bufname[128] = {0}; \
   sprintf(bufname, __VA_ARGS__); \
   RETURN_ERROR(bufname); \
   }

#define THROWS_IF_NOT_A(condition,...) if (!(condition)) { \
   char bufname[128] = {0}; \
   sprintf(bufname, __VA_ARGS__); \
   throw std::string(bufname); \
   }

using namespace v8;
using namespace node;

// properly delete a char array in the destructor
class BufferWrapper {
public:
  BufferWrapper() : buffer(NULL) { }
  BufferWrapper(char* b) : buffer(b) { }
  ~BufferWrapper() { if( buffer ) { delete[] buffer; } }
  char * buffer;
};

// free a char array in the destructor, track buffer/length
class FBuffer {
public:
  FBuffer() : buffer(NULL), length(0) { }
  FBuffer(char* b, int len) : buffer(b), length(len) { }
  ~FBuffer() { if( buffer ) { free( buffer ); } }
  char * buffer;
  int length;
};

#ifdef  WITH_GZIP
class Gzip : public ObjectWrap {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    FUNC_INIT_START;

    Local<FunctionTemplate> t = FunctionTemplate::New(ISOLATE New);

    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", GzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "deflate", GzipDeflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", GzipEnd);

    target->Set(NEW_STRING_CONST("Gzip"), t->GetFunction());
  }

  int GzipInit(int level) {
    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    // +16 to windowBits to write a simple gzip header and trailer around the
    // compressed data instead of a zlib wrapper
    return deflateInit2(&strm, level, Z_DEFLATED, 16+MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
  }

  int GzipDeflate(char* data, int data_len, FBuffer & out) {
    int ret = 0;
    char* temp;
    int i=1;

    out.buffer = NULL;
    out.length = 0;
    ret = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (Bytef*)data;
      do {
        temp = (char*) realloc(out.buffer, CHUNK*i + 1);
        if (temp == NULL) {
          return Z_MEM_ERROR;
        }
        out.buffer = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (Bytef*) out.buffer + out.length;
        ret = deflate(&strm, Z_NO_FLUSH);
        // former assert
        THROWS_IF_NOT_A (ret != Z_STREAM_ERROR, "GzipDeflate.deflate: %d", ret);  /* state not clobbered */

        out.length += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);

      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  int GzipEnd(FBuffer & out) {
    int ret;
    char* temp;
    int i = 1;

    out.buffer = NULL;
    out.length = 0;
    strm.avail_in = 0;
    strm.next_in = NULL;

    do {
      temp = (char*) realloc(out.buffer, CHUNK*i);
      if (temp == NULL) {
        return Z_MEM_ERROR;
      }
      out.buffer = temp;
      strm.avail_out = CHUNK;
      strm.next_out = (Bytef*) out.buffer + out.length;
      ret = deflate(&strm, Z_FINISH);
      // former assert
      THROWS_IF_NOT_A (ret != Z_STREAM_ERROR, "GzipEnd.deflate: %d", ret);  /* state not clobbered */

      out.length += (CHUNK - strm.avail_out);
      i++;
    } while (strm.avail_out == 0);

    // ret had better be Z_STREAM_END
    THROWS_IF_NOT_A (ret == Z_STREAM_END, "GzipEnd.deflate: %d != Z_STREAM_END", ret);
    deflateEnd(&strm);
    return ret;
  }

 protected:

  FUNC(New) {
    FUNC_START;

    Gzip *gzip = new Gzip();
    gzip->Wrap(args.This());

    RETURN_THIS;
  }

  /* options: encoding: string [null] if set output strings, else buffers
   *          level:    int    [-1]   (compression level)
   */
  FUNC(GzipInit) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    FUNC_START;

    int level = Z_DEFAULT_COMPRESSION;
    gzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(NEW_STRING_CONST("encoding"));
      Local<Value> lev = options->Get(NEW_STRING_CONST("level"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        gzip->encoding = ParseEncoding(ISOLATE enc);
        gzip->use_buffers = false;
      }
      if ((lev->IsUndefined() || lev->IsNull()) == false) {
        level = lev->Int32Value();
        THROW_IF_NOT_A (Z_NO_COMPRESSION <= level && level <= Z_BEST_COMPRESSION,
                        "invalid compression level: %d", level);
      }
    }

    int r = gzip->GzipInit(level);
    RETURN_VAL(Integer::New(ISOLATE r));
  }

  FUNC(GzipDeflate) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    FUNC_START;
    BufferWrapper bw;

    char* buf;
    ssize_t len;
    // deflate a buffer or a string?
    if (Buffer::HasInstance(args[0])) {
       // buffer
       Local<Object> buffer = args[0]->ToObject();
       len = BufferLength(buffer);
       buf = BufferData(buffer);
    } else {
      // string, default encoding is utf8
      enum encoding enc = args.Length() == 1 ? UTF8 : ParseEncoding(ISOLATE args[1], UTF8);
      len = DecodeBytes(ISOLATE args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      bw.buffer = buf = new char[len];
      ssize_t written = DecodeWrite(ISOLATE buf, len, args[0], enc);
      THROW_IF_NOT_A (written == len, "GzipDeflate.DecodeWrite: %zd != %zd", written, len);
    }

    FBuffer out;
    int r;
    try {
      r = gzip->GzipDeflate(buf, len, out);
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    THROW_IF_NOT_A (r >= 0, "gzip deflate: error(%d) %s", r, gzip->strm.msg);
    THROW_IF_NOT_A (out.length >= 0, "gzip deflate: negative output size: %d", out.length);

    if (gzip->use_buffers) {
      // output compressed data in a buffer
      BUFFERP_T b = BUFFER_NEW(out.length);
      if (out.length != 0) {
        memcpy(BufferData(b), out.buffer, out.length);
      }
      RETURN_BUFFER(b);
    } else if (out.length == 0) {
      RETURN_VAL(STRING_EMPTY);
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(ISOLATE out.buffer, out.length, gzip->encoding);
      RETURN_VAL(outString);
    }
  }

  FUNC(GzipEnd) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    FUNC_START;
    FBuffer out;
    int r;
    try {
      r = gzip->GzipEnd(out);
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    THROW_IF_NOT_A (r >= 0, "gzip end: error(%d) %s", r, gzip->strm.msg);
    THROW_IF_NOT_A (out.length >= 0, "gzip end: negative output size: %d", out.length);

    if (gzip->use_buffers) {
      // output compressed data in a buffer
      BUFFERP_T b = BUFFER_NEW(out.length);
      if (out.length != 0) {
        memcpy(BufferData(b), out.buffer, out.length);
      }
      RETURN_BUFFER(b);
    } else if (out.length == 0) {
      RETURN_VAL(STRING_EMPTY);
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(ISOLATE out.buffer, out.length, gzip->encoding);
      RETURN_VAL(outString);
    }
  }

  Gzip() : ObjectWrap(), use_buffers(true), encoding(BINARY) {
  }

  ~Gzip() {
  }

 private:

  z_stream strm;
  bool use_buffers;
  enum encoding encoding;
};

class Gunzip : public ObjectWrap {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    FUNC_INIT_START;

    Local<FunctionTemplate> t = FunctionTemplate::New(ISOLATE New);

    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", GunzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "inflate", GunzipInflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", GunzipEnd);

    target->Set(NEW_STRING_CONST("Gunzip"), t->GetFunction());
  }

  int GunzipInit() {
    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    // +16 to decode only the gzip format (no auto-header detection)
    return inflateInit2(&strm, 16+MAX_WBITS);
  }

  int GunzipInflate(const char* data, int data_len, FBuffer & out) {
    int ret = 0;
    char* temp;
    int i=1;

    out.buffer = NULL;
    out.length = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (Bytef*)data;

      do {
        temp = (char*) realloc(out.buffer, CHUNK*i);
        if (temp == NULL) {
          return Z_MEM_ERROR;
        }
        out.buffer = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (Bytef*)out.buffer + out.length;
        ret = inflate(&strm, Z_NO_FLUSH);
        // former assert
        THROWS_IF_NOT_A (ret != Z_STREAM_ERROR, "GunzipInflate.inflate: %d", ret);  /* state not clobbered */

        switch (ret) {
        case Z_NEED_DICT:
          ret = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
          (void)inflateEnd(&strm);
          return ret;
        }
        out.length += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);
      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  void GunzipEnd() {
    inflateEnd(&strm);
  }

 protected:

  FUNC(New) {
    FUNC_START;

    Gunzip *gunzip = new Gunzip();
    gunzip->Wrap(args.This());

    RETURN_THIS;
  }

  /* options: encoding: string [null], if set output strings, else buffers
   */
  FUNC(GunzipInit) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    FUNC_START;

    gunzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(NEW_STRING_CONST("encoding"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        gunzip->encoding = ParseEncoding(ISOLATE enc);
        gunzip->use_buffers = false;
      }
    }

    int r = gunzip->GunzipInit();
    RETURN_VAL(Integer::New(ISOLATE r));
  }

  FUNC(GunzipInflate) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    FUNC_START;
    BufferWrapper bw;

    char* buf;
    ssize_t len;
    // inflate a buffer or a binary string?
    if (Buffer::HasInstance(args[0])) {
       // buffer
       Local<Object> buffer = args[0]->ToObject();
       len = BufferLength(buffer);
       buf = BufferData(buffer);
    } else {
      // string, default encoding is binary. this is much worse than using a buffer
      enum encoding enc = args.Length() == 1 ? BINARY : ParseEncoding(ISOLATE args[1], BINARY);
      len = DecodeBytes(ISOLATE args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      bw.buffer = buf = new char[len];
      ssize_t written = DecodeWrite(ISOLATE buf, len, args[0], enc);
      THROW_IF_NOT_A (written == len, "GunzipInflate.DecodeWrite: %zd != %zd", written, len);
    }

    FBuffer out;
    int r;
    try {
      r = gunzip->GunzipInflate(buf, len, out);
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    THROW_IF_NOT_A (r >= 0, "gunzip inflate: error(%d) %s", r, gunzip->strm.msg);
    THROW_IF_NOT_A (out.length >= 0, "gunzip inflate: negative output size: %d", out.length);

    if (gunzip->use_buffers) {
      // output decompressed data in a buffer
      BUFFERP_T b = BUFFER_NEW(out.length);
      if (out.length != 0) {
        memcpy(BufferData(b), out.buffer, out.length);
      }
      RETURN_BUFFER(b);
    } else if (out.length == 0) {
      RETURN_VAL(STRING_EMPTY);
    } else {
      // output decompressed data in an encoded string
      Local<Value> outString = Encode(ISOLATE out.buffer, out.length, gunzip->encoding);
      RETURN_VAL(outString);
    }
  }

  FUNC(GunzipEnd) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    FUNC_START;
    try {
      gunzip->GunzipEnd();
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    RETURN_UNDEF;
  }

  Gunzip() : ObjectWrap(), use_buffers(true), encoding(BINARY) {
  }

  ~Gunzip() {
  }

 private:

  z_stream strm;
  bool use_buffers;
  enum encoding encoding;
};
#endif//WITH_GZIP


#ifdef  WITH_BZIP
class Bzip : public ObjectWrap {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    FUNC_INIT_START;

    Local<FunctionTemplate> t = FunctionTemplate::New(ISOLATE New);

    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", BzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "deflate", BzipDeflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", BzipEnd);

    target->Set(NEW_STRING_CONST("Bzip"), t->GetFunction());
  }

  int BzipInit(int level, int work) {
    /* allocate deflate state */
    strm.bzalloc = NULL;
    strm.bzfree = NULL;
    strm.opaque = NULL;
    return BZ2_bzCompressInit(&strm, level, 0, work);
  }

  int BzipDeflate(char* data, int data_len, FBuffer & out) {
    int ret = 0;
    char* temp;
    int i=1;

    out.buffer = NULL;
    out.length = 0;
    ret = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = data;
      do {
        temp = (char*) realloc(out.buffer, CHUNK*i + 1);
        if (temp == NULL) {
          return BZ_MEM_ERROR;
        }
        out.buffer = temp;
        strm.avail_out = CHUNK;
        strm.next_out = out.buffer + out.length;
        ret = BZ2_bzCompress(&strm, BZ_RUN);
        // former assert
        THROWS_IF_NOT_A (ret == BZ_RUN_OK, "BzipDeflate.BZ2_bzCompress: %d != BZ_RUN_OK", ret);

        out.length += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);

      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  int BzipEnd(FBuffer & out) {
    int ret;
    char* temp;
    int i = 1;

    out.buffer = NULL;
    out.length = 0;
    strm.avail_in = 0;
    strm.next_in = NULL;

    do {
      temp = (char*) realloc(out.buffer, CHUNK*i);
      if (temp == NULL) {
        return Z_MEM_ERROR;
      }
      out.buffer = temp;
      strm.avail_out = CHUNK;
      strm.next_out = out.buffer + out.length;
      ret = BZ2_bzCompress(&strm, BZ_FINISH);
      // former assert
      THROWS_IF_NOT_A (ret == BZ_FINISH_OK || ret == BZ_STREAM_END,
                       "BzipEnd.BZ2_bzCompress: %d != BZ_FINISH_OK || BZ_STREAM_END", ret);

      out.length += (CHUNK - strm.avail_out);
      i++;
    } while (strm.avail_out == 0);

    BZ2_bzCompressEnd(&strm);
    return ret;
  }

 protected:

  FUNC(New) {
    FUNC_START;

    Bzip *bzip = new Bzip();
    bzip->Wrap(args.This());

    RETURN_THIS;
  }

  /* options: encoding: string [null] if set output strings, else buffers
   *          level:    int    [-1]   (compression level)
   */
  FUNC(BzipInit) {
    Bzip *bzip = ObjectWrap::Unwrap<Bzip>(args.This());

    FUNC_START;

    int level = 1;
    int work = 30;
    bzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(NEW_STRING_CONST("encoding"));
      Local<Value> lev = options->Get(NEW_STRING_CONST("level"));
      Local<Value> wf = options->Get(NEW_STRING_CONST("workfactor"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        bzip->encoding = ParseEncoding(ISOLATE enc);
        bzip->use_buffers = false;
      }
      if ((lev->IsUndefined() || lev->IsNull()) == false) {
        level = lev->Int32Value();
        THROW_IF_NOT_A (1 <= level && level <= 9, "invalid compression level: %d", level);
      }
      if ((wf->IsUndefined() || wf->IsNull()) == false) {
        work = wf->Int32Value();
        THROW_IF_NOT_A (0 <= work && work <= 250, "invalid workfactor: %d", work);
      }
    }

    int r = bzip->BzipInit(level, work);
    RETURN_VAL(Integer::New(ISOLATE r));
  }

  FUNC(BzipDeflate) {
    Bzip *bzip = ObjectWrap::Unwrap<Bzip>(args.This());

    FUNC_START;
    BufferWrapper bw;

    char* buf;
    ssize_t len;
    // deflate a buffer or a string?
    if (Buffer::HasInstance(args[0])) {
       // buffer
       Local<Object> buffer = args[0]->ToObject();
       len = BufferLength(buffer);
       buf = BufferData(buffer);
    } else {
      // string, default encoding is utf8
      enum encoding enc = args.Length() == 1 ? UTF8 : ParseEncoding(ISOLATE args[1], UTF8);
      len = DecodeBytes(ISOLATE args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      bw.buffer = buf = new char[len];
      ssize_t written = DecodeWrite(ISOLATE buf, len, args[0], enc);
      THROW_IF_NOT_A (written == len, "BzipDeflate.DecodeWrite: %zd != %zd", written, len);
    }

    FBuffer out;
    int r;
    try {
      r = bzip->BzipDeflate(buf, len, out);
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    THROW_IF_NOT_A (r >= 0, "bzip deflate: error(%d)", r);
    THROW_IF_NOT_A (out.length >= 0, "bzip deflate: negative output size: %d", out.length);

    if (bzip->use_buffers) {
      // output compressed data in a buffer
      BUFFERP_T b = BUFFER_NEW(out.length);
      if (out.length != 0) {
        memcpy(BufferData(b), out.buffer, out.length);
      }
      RETURN_BUFFER(b);
    } else if (out.length == 0) {
      RETURN_VAL(STRING_EMPTY);
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(ISOLATE out.buffer, out.length, bzip->encoding);
      RETURN_VAL(outString);
    }
  }

  FUNC(BzipEnd) {
    Bzip *bzip = ObjectWrap::Unwrap<Bzip>(args.This());

    FUNC_START;
    FBuffer out;
    int r;
    try {
      r = bzip->BzipEnd(out);
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    THROW_IF_NOT_A (r >= 0, "bzip end: error(%d)", r);
    THROW_IF_NOT_A (out.length >= 0, "bzip end: negative output size: %d", out.length);

    if (bzip->use_buffers) {
      // output compressed data in a buffer
      BUFFERP_T b = BUFFER_NEW(out.length);
      if (out.length != 0) {
        memcpy(BufferData(b), out.buffer, out.length);
      }
      RETURN_BUFFER(b);
    } else if (out.length == 0) {
      RETURN_VAL(STRING_EMPTY);
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(ISOLATE out.buffer, out.length, bzip->encoding);
      RETURN_VAL(outString);
    }
  }

  Bzip() : ObjectWrap(), use_buffers(true), encoding(BINARY) {
  }

  ~Bzip() {
  }

 private:

  bz_stream strm;
  bool use_buffers;
  enum encoding encoding;
};

class Bunzip : public ObjectWrap {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    FUNC_INIT_START;

    Local<FunctionTemplate> t = FunctionTemplate::New(ISOLATE New);

    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", BunzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "inflate", BunzipInflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", BunzipEnd);

    target->Set(NEW_STRING_CONST("Bunzip"), t->GetFunction());
  }

  int BunzipInit(int small) {
    /* allocate inflate state */
    strm.bzalloc = NULL;
    strm.bzfree = NULL;
    strm.opaque = NULL;
    strm.avail_in = 0;
    strm.next_in = NULL;
    return BZ2_bzDecompressInit(&strm, 0, small);
  }

  int BunzipInflate(char* data, int data_len, FBuffer & out) {
    int ret = 0;
    char* temp;
    int i=1;

    out.buffer = NULL;
    out.length = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = data;

      do {
        temp = (char*) realloc(out.buffer, CHUNK*i);
        if (temp == NULL) {
          return BZ_MEM_ERROR;
        }
        out.buffer = temp;
        strm.avail_out = CHUNK;
        strm.next_out = out.buffer + out.length;
        ret = BZ2_bzDecompress(&strm);
        switch (ret) {
        case BZ_PARAM_ERROR:
          ret = BZ_DATA_ERROR;     /* and fall through */
        case BZ_DATA_ERROR:
        case BZ_DATA_ERROR_MAGIC:
        case BZ_MEM_ERROR:
          BZ2_bzDecompressEnd(&strm);
          return ret;
        }
        out.length += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);
      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  void BunzipEnd() {
    BZ2_bzDecompressEnd(&strm);
  }

 protected:

  FUNC(New) {
    FUNC_START;

    Bunzip *bunzip = new Bunzip();
    bunzip->Wrap(args.This());

    RETURN_THIS;
  }

  /* options: encoding:   string  [null], if set output strings, else buffers
   *          small:      boolean [false], bunzip in small mode
   */
  FUNC(BunzipInit) {
    Bunzip *bunzip = ObjectWrap::Unwrap<Bunzip>(args.This());

    FUNC_START;

    int small = 0;
    bunzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(NEW_STRING_CONST("encoding"));
      Local<Value> sm = options->Get(NEW_STRING_CONST("small"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        bunzip->encoding = ParseEncoding(ISOLATE enc);
        bunzip->use_buffers = false;
      }
      if ((sm->IsUndefined() || sm->IsNull()) == false) {
        small = sm->BooleanValue() ? 1 : 0;
      }
    }
    int r = bunzip->BunzipInit(small);
    RETURN_VAL(Integer::New(ISOLATE r));
  }

  FUNC(BunzipInflate) {
    Bunzip *bunzip = ObjectWrap::Unwrap<Bunzip>(args.This());

    FUNC_START;
    BufferWrapper bw;

    char* buf;
    ssize_t len;
    // inflate a buffer or a binary string?
    if (Buffer::HasInstance(args[0])) {
       // buffer
       Local<Object> buffer = args[0]->ToObject();
       len = BufferLength(buffer);
       buf = BufferData(buffer);
    } else {
      // string, default encoding is binary. this is much worse than using a buffer
      enum encoding enc = args.Length() == 1 ? BINARY : ParseEncoding(ISOLATE args[1], BINARY);
      len = DecodeBytes(ISOLATE args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      bw.buffer = buf = new char[len];
      ssize_t written = DecodeWrite(ISOLATE buf, len, args[0], enc);
      THROW_IF_NOT_A(written == len, "BunzipInflate.DecodeWrite: %zd != %zd", written, len);
    }

    FBuffer out;
    int r;
    try {
      r = bunzip->BunzipInflate(buf, len, out);
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    THROW_IF_NOT_A (r >= 0, "bunzip inflate: error(%d)", r);
    THROW_IF_NOT_A (out.length >= 0, "bunzip inflate: negative output size: %d", out.length);

    if (bunzip->use_buffers) {
      // output decompressed data in a buffer
      BUFFERP_T b = BUFFER_NEW(out.length);
      if (out.length != 0) {
        memcpy(BufferData(b), out.buffer, out.length);
      }
      RETURN_BUFFER(b);
    } else if (out.length == 0) {
      RETURN_VAL(STRING_EMPTY);
    } else {
      // output decompressed data in an encoded string
      Local<Value> outString = Encode(ISOLATE out.buffer, out.length, bunzip->encoding);
      RETURN_VAL(outString);
    }
  }

  FUNC(BunzipEnd) {
    Bunzip *bunzip = ObjectWrap::Unwrap<Bunzip>(args.This());

    FUNC_START;
    try {
      bunzip->BunzipEnd();
    } catch( const std::string & msg ) {
      RETURN_ERROR(msg.c_str());
    }
    RETURN_UNDEF;
  }

  Bunzip() : ObjectWrap(), use_buffers(true), encoding(BINARY) {
  }

  ~Bunzip() {
  }

 private:

  bz_stream strm;
  bool use_buffers;
  enum encoding encoding;
};
#endif//WITH_BZIP

extern "C" {
  static void init(Handle<Object> target) {
    #ifdef  WITH_GZIP
    Gzip::Initialize(target);
    Gunzip::Initialize(target);
    #endif//WITH_GZIP

    #ifdef  WITH_BZIP
    Bzip::Initialize(target);
    Bunzip::Initialize(target);
    #endif//WITH_BZIP
  }
  NODE_MODULE(gzbz2, init);
}
