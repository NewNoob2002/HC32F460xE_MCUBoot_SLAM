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
#include "DataBroker.h"
#include <cstring>
#include <limits>

DataNode* DataBroker::search(const char* id) const {
    if (!id || !*id)
        return nullptr;
    for (const auto& entry : nodes_)
        if (entry.node && std::strcmp(entry.node->getID(), id) == 0)
            return entry.node;
    return nullptr;
}
std::size_t DataBroker::nodeCount() const {
    std::size_t count = 0U;
    for (const auto& entry : nodes_)
        if (entry.node)
            ++count;
    return count;
}
int DataBroker::add(DataNode& node, const char* id) {
    if (!id || !*id)
        return DataNode::RES_PARAM_ERROR;
    if (node.isRegistered() || search(id))
        return DataNode::RES_DUPLICATE;
    if (next_serial_ == std::numeric_limits<uint64_t>::max())
        return DataNode::RES_CAPACITY;
    for (auto& entry : nodes_) {
        if (!entry.node) {
            node.serial_ = ++next_serial_;
            entry = {&node, node.serial_};
            return DataNode::RES_OK;
        }
    }
    return DataNode::RES_CAPACITY;
}
void DataBroker::remove(DataNode& node) {
    for (auto& entry : nodes_)
        if (entry.node == &node) {
            entry = {};
            return;
        }
}
bool DataBroker::alive(const Entry& entry) const {
    for (const auto& live : nodes_)
        if (entry.node && live.node == entry.node && live.serial == entry.serial)
            return true;
    return false;
}
uint32_t DataBroker::handleTimers(uint32_t now_ms) {
    if (handling_)
        return 0U;
    handling_ = true;
    const auto snapshot = nodes_;
    for (const auto& entry : snapshot) {
        if (!alive(entry) || !entry.node->timer_.expire(now_ms))
            continue;
        uint32_t event_now = now_ms;
        (void)entry.node->sendEvent(*entry.node, DataNode::EVENT_TIMER, &event_now, sizeof(event_now));
    }
    uint32_t next = UINT32_MAX;
    for (const auto& entry : nodes_) {
        if (!entry.node)
            continue;
        const uint32_t remain = entry.node->timer_.remain(now_ms);
        if (remain < next)
            next = remain;
    }
    handling_ = false;
    return next;
}
