// Minimal JNI header stub — host-only compile check of the JNI bridge
// (catches signature/name regressions without an NDK). NOT shipped.
#pragma once

#include <cstddef>
#include <cstdint>

typedef uint8_t jboolean;
typedef int8_t jbyte;
typedef uint16_t jchar;
typedef int16_t jshort;
typedef int32_t jint;
typedef int64_t jlong;
typedef float jfloat;
typedef double jdouble;
typedef jint jsize;

struct _jobject {};
struct _jclass : _jobject {};
struct _jstring : _jobject {};
struct _jarray : _jobject {};
struct _jobjectArray : _jarray {};
struct _jbyteArray : _jarray {};
struct _jintArray : _jarray {};
struct _jlongArray : _jarray {};
typedef _jobject* jobject;
typedef _jclass* jclass;
typedef _jstring* jstring;
typedef _jarray* jarray;
typedef _jobjectArray* jobjectArray;
typedef _jbyteArray* jbyteArray;
typedef _jintArray* jintArray;
typedef _jlongArray* jlongArray;

struct JNIEnv {
  jsize (*GetArrayLength)(jarray);
  void* (*GetPrimitiveArrayCritical)(jarray, jboolean*);
  void (*ReleasePrimitiveArrayCritical)(jarray, void*, jint);
  jbyte* (*GetByteArrayElements)(jbyteArray, jboolean*);
  void (*ReleaseByteArrayElements)(jbyteArray, jbyte*, jint);
  jstring (*NewString)(const jchar*, jsize);
  jintArray (*NewIntArray)(jsize);
  void (*SetIntArrayRegion)(jintArray, jsize, jsize, const jint*);
  jlongArray (*NewLongArray)(jsize);
  void (*SetLongArrayRegion)(jlongArray, jsize, jsize, const jlong*);
  jbyteArray (*NewByteArray)(jsize);
  void (*SetByteArrayRegion)(jbyteArray, jsize, jsize, const jbyte*);
  jobjectArray (*NewObjectArray)(jsize, jclass, jobject);
  void (*SetObjectArrayElement)(jobjectArray, jsize, jobject);
  jclass (*FindClass)(const char*);
  jint (*ThrowNew)(jclass, const char*);
  void (*DeleteLocalRef)(jobject);
};

#define JNIEXPORT
#define JNICALL
#define JNI_ABORT 0

extern "C" struct JNIEnv* stubEnv();
