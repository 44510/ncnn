// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "pass_level1.h"

#include "../utils.h"

namespace pnnx {

class MultiheadAttention : public FuseModulePass
{
public:
    const char* match_type_str() const
    {
        return "__torch__.torch.nn.modules.activation.MultiheadAttention";
    }

    const char* type_str() const
    {
        return "nn.MultiheadAttention";
    }

    void write(const torch::jit::Module& mod, const std::shared_ptr<torch::jit::Graph>& graph, Operator* op) const
    {
        //         mod.dump(true, true, true);

        //         graph->dump();

        const torch::jit::Node* div_num_heads = find_node_by_kind(graph, "aten::div");

        op->params["num_heads"] = div_num_heads->input(1)->node()->t(torch::jit::attr::value).item<long>();

        const torch::jit::Node* transpose_batch_seq = find_node_by_kind(graph, "aten::transpose");

        int transpose_dim0 = transpose_batch_seq->input(1)->node()->i(torch::jit::attr::value);
        int transpose_dim1 = transpose_batch_seq->input(2)->node()->i(torch::jit::attr::value);
        if (transpose_dim0 == 1 && transpose_dim1 == 0)
        {
            op->params["batch_first"] = true;
        }
        else
        {
            op->params["batch_first"] = false;
        }

        const torch::jit::Node* add_zero_attn = find_node_by_kind(graph, "aten::zeros");
        if (add_zero_attn)
        {
            op->params["add_zero_attn"] = true;
        }
        else
        {
            op->params["add_zero_attn"] = false;
        }

        const auto& in_proj_weight = mod.attr("in_proj_weight").toTensor();
        const auto& out_proj_weight = mod.attr("out_proj").toModule().attr("weight").toTensor();

        op->params["embed_dim"] = in_proj_weight.size(1);
        op->attrs["in_proj_weight"] = in_proj_weight;
        op->attrs["out_proj.weight"] = out_proj_weight;

        if (mod.hasattr("in_proj_bias") && mod.attr("out_proj").toModule().hasattr("bias"))
        {
            // bias=True
            const auto& in_proj_bias = mod.attr("in_proj_bias").toTensor();
            const auto& out_proj_bias = mod.attr("out_proj").toModule().attr("bias").toTensor();

            op->params["bias"] = true;
            op->attrs["in_proj_bias"] = in_proj_bias;
            op->attrs["out_proj.bias"] = out_proj_bias;
        }
        else
        {
            op->params["bias"] = false;
        }

        if (mod.hasattr("bias_k") && mod.hasattr("bias_v"))
        {
            // add_bias_kv=True
            const auto& bias_k = mod.attr("bias_k").toTensor();
            const auto& bias_v = mod.attr("bias_v").toTensor();

            op->params["add_bias_kv"] = true;
            op->attrs["bias_k"] = bias_k;
            op->attrs["bias_v"] = bias_v;
        }
        else
        {
            op->params["add_bias_kv"] = false;
        }
    }
};

REGISTER_GLOBAL_PNNX_FUSE_MODULE_PASS(MultiheadAttention)

} // namespace pnnx