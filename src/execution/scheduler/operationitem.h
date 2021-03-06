/*
 * Copyright 2019 Peifeng Yu <peifeng@umich.edu>
 * 
 * This file is part of Salus
 * (see https://github.com/SymbioticLab/Salus).
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SALUS_EXEC_OPERATIONITEM_H
#define SALUS_EXEC_OPERATIONITEM_H

#include <cstddef>
#include <memory>

namespace salus {
class OperationTask;
} // namespace salus

struct SessionItem;
struct OperationItem
{
    std::weak_ptr<SessionItem> sess;
    std::unique_ptr<salus::OperationTask> op;

    size_t hash() const
    {
        return reinterpret_cast<size_t>(this);
    }
};
using POpItem = std::shared_ptr<OperationItem>;

#endif // SALUS_EXEC_OPERATIONITEM_H
