#ifndef __wasilibc___wasilibc_mq_h
#define __wasilibc___wasilibc_mq_h

#ifdef __wasm64__
#define MQ_WASIX_MODULE "wasix_64v1"
#else
#define MQ_WASIX_MODULE "wasix_32v1"
#endif
#define MQ_IMPORT(name) __attribute__((__import_module__(MQ_WASIX_MODULE), __import_name__(name)))

extern int __wasilibc_mq_host_supported(void) MQ_IMPORT("mq_supported");

#endif
