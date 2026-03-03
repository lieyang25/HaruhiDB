/**
 * CXX/src/include/common/error_code.h
 */
#pragma once

#include <string>
namespace HaruhiDB
{
    enum class ErrorCode : int {
        OK = 0,
        DirError,                   // 创建目录失败
        FileOpenFailed,             // 打开文件失败
        FileCorruptedSizeMismatch,  // 文件大小不是 PAGE_SIZE 的倍数（损坏）
        FileSizeError,              // 获取文件大小失败
        HeaderWriteFailed,          // 写 header 失败
        HeaderReadFailed,           // 读 header 失败
        HeaderMagicMismatch,        // header magic mismatch
        FileNotOpen,                // 文件未打开
        ReadPageOutOfRange,         // 读页超出范围
        ReadIOError,                // 读 I/O 错误
        WriteIOError,               // 写 I/O 错误
        AllocateReadFreePageFailed, // 分配重用页：读取 free page 失败
        PersistHeaderFailed,        // 持久化 header 失败
        AllocateWriteFailed,        // 分配新页：写新页失败
        DeallocateWriteFailed,      // 释放页：写页失败
        FlushFileNotOpen            // Flush 时文件未打开
    };

    inline const char* ToString(ErrorCode c) noexcept {
        switch (c) {
            case ErrorCode::OK: return "OK";
            case ErrorCode::DirError: return "Directory error";
            case ErrorCode::FileOpenFailed: return "Failed to open file";
            case ErrorCode::FileCorruptedSizeMismatch: return "File corrupted: size mismatch";
            case ErrorCode::FileSizeError: return "File size operation error";
            case ErrorCode::HeaderWriteFailed: return "Header write failed";
            case ErrorCode::HeaderReadFailed: return "Header read failed";
            case ErrorCode::HeaderMagicMismatch: return "Header magic mismatch";
            case ErrorCode::FileNotOpen: return "File not open";
            case ErrorCode::ReadPageOutOfRange: return "Read page id out of range";
            case ErrorCode::ReadIOError: return "Read I/O error";
            case ErrorCode::WriteIOError: return "Write I/O error";
            case ErrorCode::AllocateReadFreePageFailed: return "Allocate: read free page failed";
            case ErrorCode::PersistHeaderFailed: return "Persist header failed";
            case ErrorCode::AllocateWriteFailed: return "Allocate: write new page failed";
            case ErrorCode::DeallocateWriteFailed: return "Deallocate: write failed";
            case ErrorCode::FlushFileNotOpen: return "Flush: file not open";
            default: return "Unknown Error";
        }
    }
} // namespace HaruhiDB
