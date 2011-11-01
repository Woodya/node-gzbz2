#include <memory>
#include <node.h>
#include <node_events.h>
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

#define THROW_IF_NOT(condition, text) if (!(condition)) { \
      return ThrowException(Exception::Error (String::New(text))); \
    }
#define THROW_IF_NOT_A(condition,...) if (!(condition)) { \
   char bufname[128] = {0}; \
   sprintf(bufname, __VA_ARGS__); \
   return ThrowException(Exception::Error (String::New(bufname))); \
   }

#define THROWS_IF_NOT_A(condition,...) if (!(condition)) { \
   char bufname[128] = {0}; \
   sprintf(bufname, __VA_ARGS__); \
   throw std::string(bufname); \
   }

using namespace v8;
using namespace node;

class BufferWrapper {
public:
   BufferWrapper(char* b) : buffer(b) { }
   ~BufferWrapper() { if( buffer ) { delete[] buffer; } }
   char * buffer;
};

#ifdef  WITH_GZIP
class Gzip : public EventEmitter {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", GzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "deflate", GzipDeflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", GzipEnd);

    target->Set(String::NewSymbol("Gzip"), t->GetFunction());
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

  int GzipDeflate(char* data, int data_len, char** out, int* out_len) {
    int ret = 0;
    char* temp;
    int i=1;

    *out = NULL;
    *out_len = 0;
    ret = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (Bytef*)data;
      do {
        temp = (char *)realloc(*out, CHUNK*i +1);
        if (temp == NULL) {
          return Z_MEM_ERROR;
        }
        *out = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (Bytef*)*out + *out_len;
        ret = deflate(&strm, Z_NO_FLUSH);
        // former assert
        THROWS_IF_NOT_A (ret != Z_STREAM_ERROR, "GzipDeflate.deflate: %d", ret);  /* state not clobbered */

        *out_len += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);

      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  int GzipEnd(char** out, int* out_len) {
    int ret;
    char* temp;
    int i = 1;

    *out = NULL;
    *out_len = 0;
    strm.avail_in = 0;
    strm.next_in = NULL;

    do {
      temp = (char *)realloc(*out, CHUNK*i);
      if (temp == NULL) {
        return Z_MEM_ERROR;
      }
      *out = temp;
      strm.avail_out = CHUNK;
      strm.next_out = (Bytef*)*out + *out_len;
      ret = deflate(&strm, Z_FINISH);
      // former assert
      THROWS_IF_NOT_A (ret != Z_STREAM_ERROR, "GzipEnd.deflate: %d", ret);  /* state not clobbered */

      *out_len += (CHUNK - strm.avail_out);
      i++;
    } while (strm.avail_out == 0);

    // ret had better be Z_STREAM_END
    THROWS_IF_NOT_A (ret == Z_STREAM_END, "GzipEnd.deflate: %d != Z_STREAM_END", ret);
    deflateEnd(&strm);
    return ret;
  }

 protected:

  static Handle<Value> New(const Arguments& args) {
    HandleScope scope;

    Gzip *gzip = new Gzip();
    gzip->Wrap(args.This());

    return args.This();
  }

  /* options: encoding: string [null] if set output strings, else buffers
   *          level:    int    [-1]   (compression level)
   */
  static Handle<Value> GzipInit(const Arguments& args) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    HandleScope scope;

    int level = Z_DEFAULT_COMPRESSION;
    gzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(String::NewSymbol("encoding"));
      Local<Value> lev = options->Get(String::NewSymbol("level"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        gzip->encoding = ParseEncoding(enc);
        gzip->use_buffers = false;
      }
      if ((lev->IsUndefined() || lev->IsNull()) == false) {
        level = lev->Int32Value();
        THROW_IF_NOT_A (Z_NO_COMPRESSION <= level && level <= Z_BEST_COMPRESSION,
                        "invalid compression level: %d", level);
      }
    }

    int r = gzip->GzipInit(level);
    return scope.Close(Integer::New(r));
  }

  static Handle<Value> GzipDeflate(const Arguments& args) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    HandleScope scope;
    std::auto_ptr<BufferWrapper> bw;

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
      enum encoding enc = args.Length() == 1 ? UTF8 : ParseEncoding(args[1], UTF8);
      len = DecodeBytes(args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      buf = new char[len];
      bw = std::auto_ptr<BufferWrapper>(new BufferWrapper( buf ));
      ssize_t written = DecodeWrite(buf, len, args[0], enc);
      THROW_IF_NOT_A (written == len, "GzipDeflate.DecodeWrite: %zd != %zd", written, len);
    }

    char* out = NULL;
    int r, out_size = 0;
    try {
      r = gzip->GzipDeflate(buf, len, &out, &out_size);
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    THROW_IF_NOT_A (r >= 0, "gzip deflate: error(%d) %s", r, gzip->strm.msg);
    THROW_IF_NOT_A (out_size >= 0, "gzip deflate: negative output size: %d", out_size);

    Handle<Value> returnVal;
    
    if (gzip->use_buffers) {
      // output compressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
      }
      returnVal = scope.Close(b->handle_);
    } else if (out_size == 0) {
      returnVal = scope.Close(String::Empty());
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(out, out_size, gzip->encoding);
      returnVal = scope.Close(outString);
    }
    
    if (out != NULL) {
        free(out);
    }
    
    return returnVal;
  }

  static Handle<Value> GzipEnd(const Arguments& args) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    HandleScope scope;

    char* out = NULL;
    int r, out_size;
    try {
      r = gzip->GzipEnd(&out, &out_size);
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    THROW_IF_NOT_A (r >= 0, "gzip end: error(%d) %s", r, gzip->strm.msg);
    THROW_IF_NOT_A (out_size >= 0, "gzip end: negative output size: %d", out_size);

    Handle<Value> returnVal;
    
    if (gzip->use_buffers) {
      // output compressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
      }
      returnVal = scope.Close(b->handle_);
    } else if (out_size == 0) {
      returnVal = scope.Close(String::Empty());
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(out, out_size, gzip->encoding);
      returnVal = scope.Close(outString);
    }
    
    if (out != NULL) {
        free(out);
    }
    
    return returnVal;
  }

  Gzip() : EventEmitter(), use_buffers(true), encoding(BINARY) {
  }

  ~Gzip() {
  }

 private:

  z_stream strm;
  bool use_buffers;
  enum encoding encoding;
};

class Gunzip : public EventEmitter {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", GunzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "inflate", GunzipInflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", GunzipEnd);

    target->Set(String::NewSymbol("Gunzip"), t->GetFunction());
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

  int GunzipInflate(const char* data, int data_len, char** out, int* out_len) {
    int ret = 0;
    char* temp;
    int i=1;

    *out = NULL;
    *out_len = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (Bytef*)data;

      do {
        temp = (char *)realloc(*out, CHUNK*i);
        if (temp == NULL) {
          return Z_MEM_ERROR;
        }
        *out = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (Bytef*)*out + *out_len;
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
        *out_len += (CHUNK - strm.avail_out);
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

  static Handle<Value> New(const Arguments& args) {
    HandleScope scope;

    Gunzip *gunzip = new Gunzip();
    gunzip->Wrap(args.This());

    return args.This();
  }

  /* options: encoding: string [null], if set output strings, else buffers
   */
  static Handle<Value> GunzipInit(const Arguments& args) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    HandleScope scope;

    gunzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(String::NewSymbol("encoding"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        gunzip->encoding = ParseEncoding(enc);
        gunzip->use_buffers = false;
      }
    }

    int r = gunzip->GunzipInit();
    return scope.Close(Integer::New(r));
  }

  static Handle<Value> GunzipInflate(const Arguments& args) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    HandleScope scope;
    std::auto_ptr<BufferWrapper> bw;

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
      enum encoding enc = args.Length() == 1 ? BINARY : ParseEncoding(args[1], BINARY);
      len = DecodeBytes(args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      buf = new char[len];
      bw = std::auto_ptr<BufferWrapper>(new BufferWrapper( buf ));
      ssize_t written = DecodeWrite(buf, len, args[0], enc);
      THROW_IF_NOT_A (written == len, "GunzipInflate.DecodeWrite: %zd != %zd", written, len);
    }

    char* out = NULL;
    int r, out_size;
    try {
      r = gunzip->GunzipInflate(buf, len, &out, &out_size);
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    THROW_IF_NOT_A (r >= 0, "gunzip inflate: error(%d) %s", r, gunzip->strm.msg);
    THROW_IF_NOT_A (out_size >= 0, "gunzip inflate: negative output size: %d", out_size);

    Handle<Value> returnVal;
    
    if (gunzip->use_buffers) {
      // output decompressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
      }
      returnVal = scope.Close(b->handle_);
    } else if (out_size == 0) {
      returnVal = scope.Close(String::Empty());
    } else {
      // output decompressed data in an encoded string
      Local<Value> outString = Encode(out, out_size, gunzip->encoding);
      returnVal = scope.Close(outString);
    }
    
    if (out != NULL) {
        free(out);
    }
    
    return returnVal;    
  }

  static Handle<Value> GunzipEnd(const Arguments& args) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    HandleScope scope;
    try {
      gunzip->GunzipEnd();
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    return scope.Close(Undefined());
  }

  Gunzip() : EventEmitter(), use_buffers(true), encoding(BINARY) {
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
class Bzip : public EventEmitter {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", BzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "deflate", BzipDeflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", BzipEnd);

    target->Set(String::NewSymbol("Bzip"), t->GetFunction());
  }

  int BzipInit(int level, int work) {
    /* allocate deflate state */
    strm.bzalloc = NULL;
    strm.bzfree = NULL;
    strm.opaque = NULL;
    return BZ2_bzCompressInit(&strm, level, 0, work);
  }

  int BzipDeflate(char* data, int data_len, char** out, int* out_len) {
    int ret = 0;
    char* temp;
    int i=1;

    *out = NULL;
    *out_len = 0;
    ret = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (char*)data;
      do {
        temp = (char *)realloc(*out, CHUNK*i +1);
        if (temp == NULL) {
          return BZ_MEM_ERROR;
        }
        *out = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (char*)*out + *out_len;
        ret = BZ2_bzCompress(&strm, BZ_RUN);
        // former assert
        THROWS_IF_NOT_A (ret == BZ_RUN_OK, "BzipDeflate.BZ2_bzCompress: %d != BZ_RUN_OK", ret);

        *out_len += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);

      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  int BzipEnd(char** out, int* out_len) {
    int ret;
    char* temp;
    int i = 1;

    *out = NULL;
    *out_len = 0;
    strm.avail_in = 0;
    strm.next_in = NULL;

    do {
      temp = (char *)realloc(*out, CHUNK*i);
      if (temp == NULL) {
        return Z_MEM_ERROR;
      }
      *out = temp;
      strm.avail_out = CHUNK;
      strm.next_out = (char*)*out + *out_len;
      ret = BZ2_bzCompress(&strm, BZ_FINISH);
      // former assert
      THROWS_IF_NOT_A (ret == BZ_FINISH_OK || ret == BZ_STREAM_END,
                       "BzipEnd.BZ2_bzCompress: %d != BZ_FINISH_OK || BZ_STREAM_END", ret);

      *out_len += (CHUNK - strm.avail_out);
      i++;
    } while (strm.avail_out == 0);

    BZ2_bzCompressEnd(&strm);
    return ret;
  }

 protected:

  static Handle<Value> New(const Arguments& args) {
    HandleScope scope;

    Bzip *bzip = new Bzip();
    bzip->Wrap(args.This());

    return args.This();
  }

  /* options: encoding: string [null] if set output strings, else buffers
   *          level:    int    [-1]   (compression level)
   */
  static Handle<Value> BzipInit(const Arguments& args) {
    Bzip *bzip = ObjectWrap::Unwrap<Bzip>(args.This());

    HandleScope scope;

    int level = 1;
    int work = 30;
    bzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(String::NewSymbol("encoding"));
      Local<Value> lev = options->Get(String::NewSymbol("level"));
      Local<Value> wf = options->Get(String::NewSymbol("workfactor"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        bzip->encoding = ParseEncoding(enc);
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
    return scope.Close(Integer::New(r));
  }

  static Handle<Value> BzipDeflate(const Arguments& args) {
    Bzip *bzip = ObjectWrap::Unwrap<Bzip>(args.This());

    HandleScope scope;
    std::auto_ptr<BufferWrapper> bw;

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
      enum encoding enc = args.Length() == 1 ? UTF8 : ParseEncoding(args[1], UTF8);
      len = DecodeBytes(args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      buf = new char[len];
      bw = std::auto_ptr<BufferWrapper>(new BufferWrapper( buf ));
      ssize_t written = DecodeWrite(buf, len, args[0], enc);
      THROW_IF_NOT_A (written == len, "BzipDeflate.DecodeWrite: %zd != %zd", written, len);
    }

    char* out = NULL;
    int r, out_size;
    try {
      r = bzip->BzipDeflate(buf, len, &out, &out_size);
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    THROW_IF_NOT_A (r >= 0, "bzip deflate: error(%d)", r);
    THROW_IF_NOT_A (out_size >= 0, "bzip deflate: negative output size: %d", out_size);

    Handle<Value> returnVal;
    
    if (bzip->use_buffers) {
      // output compressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
      }
      returnVal = scope.Close(b->handle_);
    } else if (out_size == 0) {
      returnVal = scope.Close(String::Empty());
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(out, out_size, bzip->encoding);
      returnVal = scope.Close(outString);
    }
    
    if (out != NULL) {
        free(out);
    }
    
    return returnVal;
  }

  static Handle<Value> BzipEnd(const Arguments& args) {
    Bzip *bzip = ObjectWrap::Unwrap<Bzip>(args.This());

    HandleScope scope;

    char* out = NULL;
    int r, out_size;
    try {
      r = bzip->BzipEnd(&out, &out_size);
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    THROW_IF_NOT_A (r >= 0, "bzip end: error(%d)", r);
    THROW_IF_NOT_A (out_size >= 0, "bzip end: negative output size: %d", out_size);

    Handle<Value> returnVal;
    
    if (bzip->use_buffers) {
      // output compressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
      }
      returnVal = scope.Close(b->handle_);
    } else if (out_size == 0) {
      returnVal = scope.Close(String::Empty());
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(out, out_size, bzip->encoding);
      returnVal = scope.Close(outString);
    }
    
    if (out != NULL) {
        free(out);
    }
    
    return returnVal;
  }

  Bzip() : EventEmitter(), use_buffers(true), encoding(BINARY) {
  }

  ~Bzip() {
  }

 private:

  bz_stream strm;
  bool use_buffers;
  enum encoding encoding;
};

class Bunzip : public EventEmitter {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", BunzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "inflate", BunzipInflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", BunzipEnd);

    target->Set(String::NewSymbol("Bunzip"), t->GetFunction());
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

  int BunzipInflate(const char* data, int data_len, char** out, int* out_len) {
    int ret = 0;
    char* temp;
    int i=1;

    *out = NULL;
    *out_len = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (char*)data;

      do {
        temp = (char *)realloc(*out, CHUNK*i);
        if (temp == NULL) {
          return BZ_MEM_ERROR;
        }
        *out = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (char*)*out + *out_len;
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
        *out_len += (CHUNK - strm.avail_out);
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

  static Handle<Value> New(const Arguments& args) {
    HandleScope scope;

    Bunzip *bunzip = new Bunzip();
    bunzip->Wrap(args.This());

    return args.This();
  }

  /* options: encoding:   string  [null], if set output strings, else buffers
   *          small:      boolean [false], bunzip in small mode
   */
  static Handle<Value> BunzipInit(const Arguments& args) {
    Bunzip *bunzip = ObjectWrap::Unwrap<Bunzip>(args.This());

    HandleScope scope;

    int small = 0;
    bunzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(String::NewSymbol("encoding"));
      Local<Value> sm = options->Get(String::NewSymbol("small"));

      if ((enc->IsUndefined() || enc->IsNull()) == false) {
        bunzip->encoding = ParseEncoding(enc);
        bunzip->use_buffers = false;
      }
      if ((sm->IsUndefined() || sm->IsNull()) == false) {
        small = sm->BooleanValue() ? 1 : 0;
      }
    }
    int r = bunzip->BunzipInit(small);
    return scope.Close(Integer::New(r));
  }

  static Handle<Value> BunzipInflate(const Arguments& args) {
    Bunzip *bunzip = ObjectWrap::Unwrap<Bunzip>(args.This());

    HandleScope scope;
    std::auto_ptr<BufferWrapper> bw;

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
      enum encoding enc = args.Length() == 1 ? BINARY : ParseEncoding(args[1], BINARY);
      len = DecodeBytes(args[0], enc);
      THROW_IF_NOT_A (len >= 0, "invalid DecodeBytes result: %zd", len);

      buf = new char[len];
      bw = std::auto_ptr<BufferWrapper>(new BufferWrapper( buf ));
      ssize_t written = DecodeWrite(buf, len, args[0], enc);
      THROW_IF_NOT_A(written == len, "BunzipInflate.DecodeWrite: %zd != %zd", written, len);
    }

    char* out = NULL;
    int r, out_size;
    try {
      r = bunzip->BunzipInflate(buf, len, &out, &out_size);
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    THROW_IF_NOT_A (r >= 0, "bunzip inflate: error(%d)", r);
    THROW_IF_NOT_A (out_size >= 0, "bunzip inflate: negative output size: %d", out_size);

    Handle<Value> returnVal;
    
    if (bunzip->use_buffers) {
      // output decompressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
      }
      returnVal = scope.Close(b->handle_);
    } else if (out_size == 0) {
      returnVal = scope.Close(String::Empty());
    } else {
      // output decompressed data in an encoded string
      Local<Value> outString = Encode(out, out_size, bunzip->encoding);
      returnVal = scope.Close(outString);
    }
    
    if (out != NULL) {
        free(out);
    }
    
    return returnVal;
  }

  static Handle<Value> BunzipEnd(const Arguments& args) {
    Bunzip *bunzip = ObjectWrap::Unwrap<Bunzip>(args.This());

    HandleScope scope;
    try {
      bunzip->BunzipEnd();
    } catch( const std::string & msg ) {
      return ThrowException(Exception::Error (String::New(msg.c_str())));
    }
    return scope.Close(Undefined());
  }

  Bunzip() : EventEmitter(), use_buffers(true), encoding(BINARY) {
  }

  ~Bunzip() {
  }

 private:

  bz_stream strm;
  bool use_buffers;
  enum encoding encoding;
};
#endif//WITH_BZIP

extern "C" void init(Handle<Object> target) {
  HandleScope scope;
  #ifdef  WITH_BZIP
  Gzip::Initialize(target);
  Gunzip::Initialize(target);
  #endif//WITH_GZIP

  #ifdef  WITH_BZIP
  Bzip::Initialize(target);
  Bunzip::Initialize(target);
  #endif//WITH_BZIP
}
