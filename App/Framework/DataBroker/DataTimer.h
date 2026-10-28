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
#include <cstdint>

/** 节点内嵌定时器；无符号时间差支持毫秒计数回绕，不追赶遗漏周期。 */
class DataTimer {
  public:
    void start(uint32_t period_ms, uint32_t now_ms) {
        period_ = period_ms;
        last_ = now_ms;
        running_ = period_ms != 0U;
    }
    void stop() {
        running_ = false;
    }
    bool running() const {
        return running_;
    }
    uint32_t remain(uint32_t now_ms) const {
        if (!running_)
            return UINT32_MAX;
        const uint32_t elapsed = now_ms - last_;
        return elapsed >= period_ ? 0U : period_ - elapsed;
    }
    bool expire(uint32_t now_ms) {
        if (!running_ || remain(now_ms) != 0U)
            return false;
        last_ = now_ms;
        return true;
    }

  private:
    uint32_t period_ = 0U;
    uint32_t last_ = 0U;
    bool running_ = false;
};
