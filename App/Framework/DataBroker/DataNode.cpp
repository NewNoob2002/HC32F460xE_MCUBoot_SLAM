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
#include "DataNode.h"
#include <algorithm>
#include <cstring>
#include "DataBroker.h"

namespace {
template <std::size_t N> DataNode* search(const std::array<DataNode*, N>& list, const char* id) {
    if (!id || !*id)
        return nullptr;
    for (auto* node : list)
        if (node && std::strcmp(node->getID(), id) == 0)
            return node;
    return nullptr;
}
template <std::size_t N> bool contains(const std::array<DataNode*, N>& list, const DataNode* node) {
    return std::find(list.begin(), list.end(), node) != list.end();
}
template <std::size_t N> bool append(std::array<DataNode*, N>& list, DataNode* node) {
    auto it = std::find(list.begin(), list.end(), nullptr);
    if (it == list.end())
        return false;
    *it = node;
    return true;
}
template <std::size_t N> void remove(std::array<DataNode*, N>& list, DataNode* node) {
    auto it = std::find(list.begin(), list.end(), node);
    if (it == list.end())
        return;
    std::move(it + 1, list.end(), it);
    list.back() = nullptr;
}
template <std::size_t N> std::size_t count(const std::array<DataNode*, N>& list) {
    std::size_t result = 0U;
    for (auto* node : list)
        if (node)
            ++result;
    return result;
}
} // namespace
DataNode::~DataNode() {
    if (!broker_)
        return;
    for (auto* sub : subscribers_)
        if (sub)
            remove(sub->publishers_, this);
    for (auto* pub : publishers_)
        if (pub)
            remove(pub->subscribers_, this);
    broker_->remove(*this);
}
int DataNode::init(DataBroker& broker, const char* id, uint32_t filter, Callback callback, void* user_data) {
    const int result = broker.add(*this, id);
    if (result != RES_OK)
        return result;
    id_ = id;
    broker_ = &broker;
    filter_ = filter;
    callback_ = callback;
    user_data_ = user_data;
    return RES_OK;
}
int DataNode::subscribe(const char* id) {
    if (!broker_ || !id || !*id || std::strcmp(id_, id) == 0)
        return RES_PARAM_ERROR;
    if (search(publishers_, id))
        return RES_DUPLICATE;
    auto* pub = broker_->search(id);
    if (!pub)
        return RES_NOT_FOUND;
    if (!append(publishers_, pub))
        return RES_CAPACITY;
    if (!append(pub->subscribers_, this)) {
        remove(publishers_, pub);
        return RES_CAPACITY;
    }
    return RES_OK;
}
int DataNode::unsubscribe(const char* id) {
    if (!broker_ || !id || !*id)
        return RES_PARAM_ERROR;
    auto* pub = search(publishers_, id);
    if (!pub)
        return RES_NOT_FOUND;
    remove(publishers_, pub);
    remove(pub->subscribers_, this);
    return RES_OK;
}
std::size_t DataNode::publisherCount() const {
    return count(publishers_);
}
std::size_t DataNode::subscriberCount() const {
    return count(subscribers_);
}
int DataNode::sendEvent(DataNode& receiver, EventCode event, void* data, std::size_t size) {
    if ((receiver.filter_ & event) == 0U)
        return RES_UNSUPPORTED_REQUEST;
    EventParam param{this, &receiver, event, data, size};
    return receiver.onEvent(&param);
}
int DataNode::onEvent(EventParam* event) {
    return callback_ ? callback_(this, event) : RES_NO_CALLBACK;
}
int DataNode::publish(const void* data, std::size_t size) {
    if (!broker_ || !data || !size)
        return RES_PARAM_ERROR;
    std::array<DataBroker::Entry, MaxLinks> snapshot{};
    for (std::size_t i = 0U; i < subscribers_.size(); ++i)
        if (subscribers_[i])
            snapshot[i] = {subscribers_[i], subscribers_[i]->serial_};
    int result = RES_OK;
    for (const auto& entry : snapshot) {
        if (!broker_->alive(entry) || !contains(subscribers_, entry.node))
            continue;
        // Subscribers filtering PUBLISH only pull; explicit callback errors still propagate to the caller.
        if ((entry.node->filter_ & EVENT_PUBLISH) == 0U)
            continue;
        const int delivered = sendEvent(*entry.node, EVENT_PUBLISH, const_cast<void*>(data), size);
        if (delivered == RES_STOP_PROCESS)
            return delivered;
        if (delivered != RES_OK)
            result = delivered;
    }
    return result;
}
int DataNode::pull(const char* id, void* data, std::size_t size) {
    if (!broker_ || !data || !size)
        return RES_PARAM_ERROR;
    auto* pub = search(publishers_, id);
    return pub ? sendEvent(*pub, EVENT_PULL, data, size) : RES_NOT_FOUND;
}
int DataNode::notify(const char* id, const void* data, std::size_t size) {
    if (!broker_ || !data || !size)
        return RES_PARAM_ERROR;
    auto* pub = search(publishers_, id);
    return pub ? sendEvent(*pub, EVENT_NOTIFY, const_cast<void*>(data), size) : RES_NOT_FOUND;
}
int DataNode::replyPull(const EventParam* event, const void* state, std::size_t size) {
    if (!event || event->event != EVENT_PULL || !event->data || !state)
        return RES_PARAM_ERROR;
    if (event->size != size)
        return RES_SIZE_MISMATCH;
    std::memcpy(event->data, state, size);
    return RES_OK;
}
