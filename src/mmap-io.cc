/*
    Licensed under The MIT License (MIT)
    You will find the full license legal mumbo jumbo in file "LICENSE"

    Copyright (c) 2015 - 2018 Oscar Campbell

    Inspired by Ben Noordhuis module node-mmap - which does the same thing for older node
    versions, sans advise and sync.
*/
#include <napi.h>
#include <uv.h>
#include <errno.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include "mman.h"
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

/* #include <future> */

using namespace Napi;

// Just a bit more clear as to intent
#define JS_FN(a) Napi::Value a(const Napi::CallbackInfo &info)

// This lib is one of those pieces of code where clarity is better then puny micro opts (in
// comparison to the massive blocking that will occur when the data is first read from disk)
// Since casting `size` to `void*` feels a little "out there" considering that void* may be
// 32b or 64b (or, I dunno, 47b on some quant particle system), we throw this struct in.
struct MMap
{
    MMap(char *data, size_t size) : data(data), size(size) {}
    char *data = nullptr;
    size_t size = 0;
};

void do_mmap_cleanup(Napi::Env env, char *data, void *hint)
{
    auto map_info = static_cast<MMap *>(hint);
    munmap(data, map_info->size);
    delete map_info;
}

inline int do_mmap_advice(char *addr, size_t length, int advise)
{
    return madvise(static_cast<void *>(addr), length, advise);
}

JS_FN(mmap_map)
{
    Napi::Env env = info.Env();

    if (info.Length() < 4 && info.Length() > 6)
    {
        Napi::Error::New(env,
                         "map() takes 4, 5 or 6 arguments: (size :int, protection :int, flags :int, fd :int [, offset :int [, advise :int]]).")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    // Try to be a little (motherly) helpful to us poor clueless developers
    if (!info[0].IsNumber())
    {
        Napi::Error::New(env, "mmap: size (arg[0]) must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[1].IsNumber())
    {
        Napi::Error::New(env, "mmap: protection_flags (arg[1]) must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[2].IsNumber())
    {
        Napi::Error::New(env, "mmap: flags (arg[2]) must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[3].IsNumber())
    {
        Napi::Error::New(env, "mmap: fd (arg[3]) must be an integer (a file descriptor)").ThrowAsJavaScriptException();
        return env.Null();
    }
    // Offset and advise are optional

    constexpr void *hinted_address = nullptr; // Just making things uber-clear...
    const size_t size = static_cast<size_t>(info[0].As<Napi::Number>().Int64Value());
    const int protection = info[1].As<Napi::Number>().Int32Value();
    const int flags = info[2].As<Napi::Number>().Int32Value();
    const int fd = info[3].As<Napi::Number>().Int32Value();
    const size_t offset = static_cast<size_t>(info[4].IsUndefined() ? 0 : info[4].As<Napi::Number>().Int64Value());
    const int advise = info[5].IsUndefined() ? 0 : info[5].As<Napi::Number>().Int32Value();

    char *data = static_cast<char *>(mmap(hinted_address, size, protection, flags, fd, offset));

    if (data == MAP_FAILED)
    {
        Napi::Error::New(env, (std::string("mmap failed, ") + std::to_string(errno)).c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }
    else
    {
        if (advise != 0)
        {
            auto ret = do_mmap_advice(data, size, advise);
            if (ret)
            {
                Napi::Error::New(env, (std::string("madvise() failed, ") + std::to_string(errno)).c_str()).ThrowAsJavaScriptException();
                return env.Null();
            }

            //     // Asynchronous read-ahead to minimisze blocking. This
            //     // has worked flawless, but is not necessary, and any
            //     // gains are speculative.
            //     //
            //     // Play with it if you want to.
            //     //
            //     std::async(std::launch::async, [=](){
            //         auto ret = do_mmap_advice(data, size, advise);
            //         if (ret) {
            //             Napi::Error::New(env, (std::string("madvise() failed, ") + std::to_string(errno)).c_str()).ThrowAsJavaScriptException();
            //             return env.Null();
            //         }
            //         readahead(fd, offset, 1024 * 1024 * 4);
            //     });
        }

        auto map_info = new MMap(data, size);
        Napi::Object buf = Napi::Buffer<char>::New(env, data, size, &do_mmap_cleanup, map_info).ToObject();
        if (buf.IsEmpty())
        {
            Napi::Error::New(env, "couldn't allocate Node Buffer()").ThrowAsJavaScriptException();
            return env.Null();
        }
        else
        {
            return buf;
        }
    }
}

JS_FN(mmap_advise)
{
    Napi::Env env = info.Env();

    if (info.Length() != 2 && info.Length() != 4)
    {
        Napi::Error::New(env,
                         "advise() takes 2 or 4 arguments: (buffer :Buffer, advise :int) | (buffer :Buffer, offset :int, length :int, advise :int).")
            .ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[0].IsObject())
    {
        Napi::Error::New(env, "advice(): buffer (arg[0]) must be a Buffer").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[1].IsNumber())
    {
        Napi::Error::New(env, "advice(): (arg[1]) must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object buf = info[0].ToObject();
    char *data = buf.As<Napi::Buffer<char>>().Data();
    size_t size = buf.As<Napi::Buffer<char>>().Length();

    int ret = ([&]() -> int
               {
        if (info.Length() == 2) {
            int advise = info[1].As<Napi::Number>().Int32Value();
            return do_mmap_advice(data, size, advise);
        }
        else {
            int offset = info[1].As<Napi::Number>().Int32Value();
            int length = info[2].As<Napi::Number>().Int32Value();
            int advise = info[3].As<Napi::Number>().Int32Value();
            return do_mmap_advice(data + offset, length, advise);
        } })();

    if (ret)
    {
        Napi::Error::New(env, (std::string("madvise() failed, ") + std::to_string(errno)).c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }

    return env.Undefined();
}

JS_FN(mmap_incore)
{
    Napi::Env env = info.Env();

    if (info.Length() != 1)
    {
        Napi::Error::New(env, "incore() takes 1 argument: (buffer :Buffer) .").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsObject())
    {
        Napi::Error::New(env, "advice(): buffer (arg[0]) must be a Buffer").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object buf = info[0].ToObject();
    char *data = buf.As<Napi::Buffer<char>>().Data();
    size_t size = buf.As<Napi::Buffer<char>>().Length();

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    size_t page_size = sysinfo.dwPageSize;
#else
    size_t page_size = sysconf(_SC_PAGESIZE);
#endif

    size_t needed_bytes = (size + page_size - 1) / page_size;
    size_t pages = size / page_size;

#ifdef __APPLE__
    char *result_data = static_cast<char *>(malloc(needed_bytes));
#else
    unsigned char *result_data = static_cast<unsigned char *>(malloc(needed_bytes));
#endif

    if (size % page_size > 0)
    {
        pages++;
    }

    int ret = mincore(data, size, result_data);

    if (ret)
    {
        free(result_data);
        if (errno == ENOSYS)
        {
            Napi::Error::New(env, "mincore() not implemented").ThrowAsJavaScriptException();
            return env.Null();
        }
        else
        {
            Napi::Error::New(env, (std::string("mincore() failed, ") + std::to_string(errno)).c_str()).ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    // Now we want to check all of the pages
    uint32_t pages_mapped = 0;
    uint32_t pages_unmapped = 0;

    for (size_t i = 0; i < pages; i++)
    {
        if (!(result_data[i] & 0x1))
        {
            pages_unmapped++;
        }
        else
        {
            pages_mapped++;
        }
    }

    free(result_data);

    Napi::Array arr = Napi::Array::New(env, 2);
    uint32_t index = 0;
    arr[index] = Napi::Number::New(env, pages_unmapped);
    arr[index + 1] = Napi::Number::New(env, pages_mapped);
    return arr;
}

JS_FN(mmap_sync_lib_private_)
{
    Napi::Env env = info.Env();

    // I barfed at the thought of implementing all variants of info-combos in C++, so
    // the arg-shuffling and checking is done in a ES wrapper function - see "mmap-io.ts"
    if (info.Length() != 5)
    {
        Napi::Error::New(env, "sync() takes 5 arguments: (buffer :Buffer, offset :int, length :int, do_blocking_sync :bool, invalidate_pages_and_signal_refresh_to_consumers :bool).")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsObject())
    {
        Napi::Error::New(env, "sync(): buffer (arg[0]) must be a Buffer").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object buf = info[0].ToObject();
    char *data = buf.As<Napi::Buffer<char>>().Data();

    int offset = info[1].IsUndefined() ? 0 : info[1].As<Napi::Number>().Int32Value();
    size_t length = info[2].IsUndefined() ? 0 : info[2].As<Napi::Number>().Int32Value();
    bool blocking_sync = info[3].IsUndefined() ? false : info[3].As<Napi::Boolean>().Value();
    bool invalidate = info[4].IsUndefined() ? false : info[4].As<Napi::Boolean>().Value();
    int flags = ((blocking_sync ? MS_SYNC : MS_ASYNC) | (invalidate ? MS_INVALIDATE : 0));

    int ret = msync(data + offset, length, flags);

    if (ret)
    {
        Napi::Error::New(env, (std::string("msync() failed, ") + std::to_string(errno)).c_str()).ThrowAsJavaScriptException();
        return env.Null();
    }
    return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set(Napi::String::New(env, "PROT_READ"),
                Napi::Number::New(env, PROT_READ));
    exports.Set(Napi::String::New(env, "PROT_WRITE"),
                Napi::Number::New(env, PROT_WRITE));
    exports.Set(Napi::String::New(env, "PROT_EXEC"),
                Napi::Number::New(env, PROT_EXEC));
    exports.Set(Napi::String::New(env, "PROT_NONE"),
                Napi::Number::New(env, PROT_NONE));

    exports.Set(Napi::String::New(env, "MAP_SHARED"),
                Napi::Number::New(env, MAP_SHARED));
    exports.Set(Napi::String::New(env, "MAP_PRIVATE"),
                Napi::Number::New(env, MAP_PRIVATE));

#ifdef MAP_NONBLOCK
    // set_int_prop("MAP_NONBLOCK", MAP_NONBLOCK);
    exports.Set(Napi::String::New(env, "MAP_NONBLOCK"),
                Napi::Number::New(env, MAP_NONBLOCK));
#endif

#ifdef MAP_POPULATE
    // set_int_prop("MAP_POPULATE", MAP_POPULATE);
    exports.Set(Napi::String::New(env, "MAP_POPULATE"),
                Napi::Number::New(env, MAP_POPULATE));
#endif

    exports.Set(Napi::String::New(env, "MADV_NORMAL"),
                Napi::Number::New(env, MADV_NORMAL));
    exports.Set(Napi::String::New(env, "MADV_RANDOM"),
                Napi::Number::New(env, MADV_RANDOM));
    exports.Set(Napi::String::New(env, "MADV_SEQUENTIAL"),
                Napi::Number::New(env, MADV_SEQUENTIAL));
    exports.Set(Napi::String::New(env, "MADV_WILLNEED"),
                Napi::Number::New(env, MADV_WILLNEED));
    exports.Set(Napi::String::New(env, "MADV_DONTNEED"),
                Napi::Number::New(env, MADV_DONTNEED));

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    exports.Set(Napi::String::New(env, "PAGESIZE"),
                Napi::Number::New(env, sysinfo.dwPageSize));
#else
    exports.Set(Napi::String::New(env, "PAGESIZE"),
                Napi::Number::New(env, sysconf(_SC_PAGESIZE)));
#endif

    exports.Set(Napi::String::New(env, "map"),
                Napi::Function::New(env, mmap_map));
    exports.Set(Napi::String::New(env, "advise"),
                Napi::Function::New(env, mmap_advise));
    exports.Set(Napi::String::New(env, "incore"),
                Napi::Function::New(env, mmap_incore));

    // This one is wrapped by a JS-function and deleted from obj to hide from user
    exports.Set(Napi::String::New(env, "sync_lib_private__"),
                Napi::Function::New(env, mmap_sync_lib_private_));

    return exports;
}

NODE_API_MODULE(mmap_io, Init)
