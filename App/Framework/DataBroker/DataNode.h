/*
 * MIT License
 * Copyright (c) 2021 - 2024 _VIFEXTech
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "DataTimer.h"

class DataBroker;
/** 应用主循环独占。载荷仅在同步调用期间有效；禁止销毁当前发布者或 broker。
 * 对象由调用方持有，broker 必须比节点长寿；不允许经基类指针销毁节点。
 */
class DataNode {
  public:
    enum EventCode : uint32_t {
        EVENT_NONE = 0U,
        EVENT_PUBLISH = 1U,
        EVENT_PULL = 2U,
        EVENT_NOTIFY = 4U,
        EVENT_TIMER = 8U,
        EVENT_ALL = 0x0FU
    };
    enum Result {
        RES_OK = 0,
        RES_UNKNOWN = -1,
        RES_SIZE_MISMATCH = -2,
        RES_UNSUPPORTED_REQUEST = -3,
        RES_NO_CALLBACK = -4,
        RES_NO_DATA = -5,
        RES_NOT_FOUND = -6,
        RES_PARAM_ERROR = -7,
        RES_STOP_PROCESS = -8,
        RES_CAPACITY = -9,
        RES_DUPLICATE = -10
    };
    struct EventParam {
        DataNode* tran;
        DataNode* recv;
        EventCode event;
        void* data;
        std::size_t size;
    };
    using Callback = int (*)(DataNode*, EventParam*);
    static constexpr std::size_t MaxLinks = 8U;
    constexpr DataNode() = default;
    ~DataNode();
    DataNode(const DataNode&) = delete;
    DataNode& operator=(const DataNode&) = delete;
    /** id 字符串必须比节点长寿。失败不改变已有节点或关系。 */
    int init(DataBroker& broker, const char* id, uint32_t filter = EVENT_ALL, Callback callback = nullptr,
             void* user_data = nullptr);
    bool isRegistered() const {
        return broker_ != nullptr;
    }
    const char* getID() const {
        return id_;
    }
    void* getUserData() const {
        return user_data_;
    }
    int subscribe(const char* id);
    int unsubscribe(const char* id);
    std::size_t publisherCount() const;
    std::size_t subscriberCount() const;
    int publish(const void* data, std::size_t size);
    int pull(const char* id, void* data, std::size_t size);
    int notify(const char* id, const void* data, std::size_t size);
    void startTimer(uint32_t period_ms, uint32_t now_ms) {
        if (isRegistered())
            timer_.start(period_ms, now_ms);
    }
    void stopTimer() {
        timer_.stop();
    }
    bool hasTimer() const {
        return timer_.running();
    }
    static int replyPull(const EventParam* event, const void* state, std::size_t size);
    virtual int onEvent(EventParam* event);

  private:
    friend class DataBroker;
    using Links = std::array<DataNode*, MaxLinks>;
    Links publishers_{};
    Links subscribers_{};
    const char* id_ = nullptr;
    DataBroker* broker_ = nullptr;
    void* user_data_ = nullptr;
    Callback callback_ = nullptr;
    uint32_t filter_ = EVENT_ALL;
    uint64_t serial_ = 0U;
    DataTimer timer_{};
    int sendEvent(DataNode& receiver, EventCode event, void* data, std::size_t size);
};
