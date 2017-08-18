#ifndef BUFFER_COMPAT_H
#define BUFFER_COMPAT_H

#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>

#if NODE_MINOR_VERSION < 3 && NODE_MAJOR_VERSION < 1

char *BufferData(node::Buffer *b) {
    return b->data();
}
size_t BufferLength(node::Buffer *b) {
    return b->length();
}
char *BufferData(v8::Local<v8::Object> buf_obj) {
    v8::HandleScope scope;
    node::Buffer *buf = node::ObjectWrap::Unwrap<node::Buffer>(buf_obj);
    return buf->data();
}
size_t BufferLength(v8::Local<v8::Object> buf_obj) {
    v8::HandleScope scope;
    node::Buffer *buf = node::ObjectWrap::Unwrap<node::Buffer>(buf_obj);
    return buf->length();
}

#elif NODE_MAJOR_VERSION < 3

char *BufferData(node::Buffer *b) {
    return node::Buffer::Data(b->handle_);
}
size_t BufferLength(node::Buffer *b) {
    return node::Buffer::Length(b->handle_);
}
char *BufferData(v8::Local<v8::Object> buf_obj) {
    v8::HandleScope scope;
    return node::Buffer::Data(buf_obj);
}
size_t BufferLength(v8::Local<v8::Object> buf_obj) {
    v8::HandleScope scope;
    return node::Buffer::Length(buf_obj);
}

#else // NODE_VERSION

#define BufferData node::Buffer::Data
#define BufferLength node::Buffer::Length

#endif // NODE_VERSION

#endif//BUFFER_COMPAT_H
