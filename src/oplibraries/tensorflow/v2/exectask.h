/*
 * <one line to give the library's name and an idea of what it does.>
 * Copyright (C) 2017  Aetf <aetf@unlimitedcodeworks.xyz>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EXECTASK_H
#define EXECTASK_H

#include "md_executor_impl.h"

#include "execution/operationtask.h"
#include "utils/pointerutils.h"

#include "oplibraries/tensorflow/tensorflow_headers.h"

class PerOpAllocDevice;
struct DeviceItem
{
    std::shared_ptr<PerOpAllocDevice> device = nullptr;
    std::shared_ptr<tf::FunctionLibraryRuntime> function_library = nullptr;
    bool device_record_tensor_access = false;
};

namespace utils {
class semaphore;
} // namespace

/**
 * @todo write docs
 */
class ExecTask : public OperationTask
{
public:
    ExecTask(ExecutorState *state, utils::semaphore &num_finished_ops,
             ExecutorState::TaggedNode &node, ExecutorState::TaggedNodeSeq &ready,
             ExecutorState::TaggedNodeReadyQueue &inline_ready,
             tf::NodeExecStats *stats, tf::OpKernelContext::Params &params,
             int64_t &scheduled_usec,
             ExecutorState::EntryVector &outputs,
             TensorValueVec &inputs,
             DeviceContextVec &input_device_contexts,
             AllocatorAttributeVec &input_alloc_attrs,
             bool &completed, tf::Rendezvous *rendez, int maxFailures = 2);

    bool prepare(const ResourceContext &rctx) override;

    void run(Callbacks cbs) override;

    void afterRun(const tf::Status &s, const Callbacks &cbs);

    int failedTimes() const override { return failureTimes; }

    Resources estimatedUsage(const DeviceSpec &dev) override;

    void releasePreAllocation() override;

    const std::vector<DeviceType> &supportedDeviceTypes() const override;

    ~ExecTask() override;

    std::string DebugString() override;

private:
    tensorflow::Status LookupDevice(const DeviceSpec &spec, DeviceItem &item);

    bool maybeMemoryFailure(const tf::Status &s, DoneCallback memFailure);

private:
    ResourceContext rctx;
    DeviceItem ditem;
    std::unordered_map<DeviceSpec, Resources> cachedUsage;
    std::vector<DeviceType> supportedTypes;
    std::function<void(tf::OpKernel*, tf::FunctionLibraryRuntime*)> deleteKernel;

    std::unique_ptr<PerOpAllocDevice, std::function<void(PerOpAllocDevice*)>> wrappedDevice;

    int failureTimes = 0;
    int maxFailures;

    tf::OpKernel *op_kernel = nullptr;
    bool kernel_is_async;
    bool has_ref_input;

    // Borrowed from ExecutorState
    ExecutorState::TaggedNode &tagged_node;
    ExecutorState::TaggedNodeSeq &ready;
    ExecutorState::TaggedNodeReadyQueue &inline_ready;
    tf::NodeExecStats *stats;
    tf::OpKernelContext::Params &params;
    int64_t &scheduled_usec;
    ExecutorState::EntryVector &outputs;
    TensorValueVec &inputs;
    DeviceContextVec &input_device_contexts;
    AllocatorAttributeVec &input_alloc_attrs;
    bool &completed;
    tf::Rendezvous *rendez;
    utils::semaphore &num_finished_ops;

    ExecutorState *m_state;
};

#endif // EXECTASK_H
