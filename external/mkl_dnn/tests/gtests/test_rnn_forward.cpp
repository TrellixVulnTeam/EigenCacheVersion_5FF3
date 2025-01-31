/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include <utility>
#include <numeric>

#include "gtest/gtest.h"
#include "mkldnn_test_common.hpp"

#include "mkldnn.hpp"

namespace mkldnn {

struct test_rnn_sizes_t {
    test_rnn_sizes_t(
        int l, int d, int t, int mb,
        int slc, int sic, int dlc, int dic) :
        l(l), d(d), t(t), mb(mb),
        slc(slc), sic(sic), dlc(dlc), dic(dic) {}
    int l, d;
    int t;
    int mb;
    int slc, sic, dlc, dic;
};

struct test_rnn_formats_t {
    mkldnn::memory::format src_layer_fmt;
    mkldnn::memory::format src_iter_fmt;
    mkldnn::memory::format weights_layer_fmt;
    mkldnn::memory::format weights_iter_fmt;
    mkldnn::memory::format bias_fmt;
    mkldnn::memory::format dst_layer_fmt;
    mkldnn::memory::format dst_iter_fmt;
};

struct test_rnn_params_t {
    const mkldnn::engine::kind engine_kind;
    mkldnn::algorithm aalgorithm;
    mkldnn::algorithm activation;
    mkldnn::rnn_direction direction;
    test_rnn_formats_t fmts;
    test_rnn_sizes_t sizes;
    bool expect_to_fail;
    mkldnn_status_t expected_status;
};

// We assume uniform data type accross tensors for now
template <typename data_t>
class rnn_forward_test
    : public ::testing::TestWithParam<test_rnn_params_t> {
protected:
    virtual void SetUp() {
        auto p = ::testing::TestWithParam<test_rnn_params_t>::GetParam();
        catch_expected_failures([=](){Test();}, p.expect_to_fail,
                p.expected_status, false);
    }

    void Test() {
        auto p = ::testing::TestWithParam<test_rnn_params_t>::GetParam();
        ASSERT_TRUE(p.engine_kind == engine::kind::cpu);
        auto eng = engine(p.engine_kind, 0);
        //@todo check algorithm is one of the supported by RNN
        //ASSERT_EQ(p.aalgorithm, algorithm::vanilla_lstm);

        // Initialize the data
        memory::data_type prec = data_traits<data_t>::data_type;
        auto dims = p.sizes;
        auto t = dims.t, mb = dims.mb, l = dims.l, d = dims.d;
        auto slc = dims.slc, sic = dims.sic, dlc = dims.dlc, dic = dims.dic;
        int s, g, bias_extra_gate;

        switch (p.aalgorithm) {
        case vanilla_lstm:
            g = 4; s = 2; break;
        case vanilla_gru:
        case gru_linear_before_reset:
            g = 3; s = 1; break;
        default:
            g = 1; s = 1; break;
        };

        bias_extra_gate = p.aalgorithm == gru_linear_before_reset ? 1 : 0;

        auto weights_layer_dims = {l, d, slc, g, dic};
        auto weights_iter_dims = {l, d, sic, g, dic};
        auto bias_dims = {l, d, g + bias_extra_gate, dic};
        auto src_layer_dims = {t, mb, slc};
        auto src_iter_dims = {l, d, s, mb, sic};
        auto dst_layer_dims = {t, mb, dlc};
        auto dst_iter_dims = {l, d, s, mb, dic};

        auto weights_layer_md_any = memory::desc({weights_layer_dims}, prec, memory::format::any);
        auto weights_iter_md_any = memory::desc({weights_iter_dims}, prec, memory::format::any);
        auto bias_md_any = memory::desc({bias_dims}, prec, memory::format::any);
        auto src_layer_md_any = memory::desc({src_layer_dims}, prec, memory::format::any);
        auto src_iter_md_any = memory::desc({src_iter_dims}, prec, memory::format::any);
        auto dst_layer_md_any = memory::desc({dst_layer_dims}, prec, memory::format::any);
        auto dst_iter_md_any = memory::desc({dst_iter_dims}, prec, memory::format::any);

        auto weights_layer_md_tgt = memory::desc({weights_layer_dims}, prec, p.fmts.weights_layer_fmt);
        auto weights_iter_md_tgt = memory::desc({weights_iter_dims}, prec, p.fmts.weights_iter_fmt);
        auto bias_md_tgt = memory::desc({bias_dims}, prec, p.fmts.bias_fmt);
        auto src_layer_md_tgt = memory::desc({src_layer_dims}, prec, p.fmts.src_layer_fmt);
        auto src_iter_md_tgt = memory::desc({src_iter_dims}, prec, p.fmts.src_iter_fmt);
        auto dst_layer_md_tgt = memory::desc({dst_layer_dims}, prec, p.fmts.dst_layer_fmt);
        auto dst_iter_md_tgt = memory::desc({dst_iter_dims}, prec, p.fmts.dst_iter_fmt);


        // Create the reference descriptor
        rnn_cell::desc cell(p.aalgorithm, p.activation);
        auto direction = p.direction;

        rnn_forward::desc ref_desc(prop_kind::forward_inference, cell,
                direction, src_layer_md_any, src_iter_md_any,
                weights_layer_md_any, weights_iter_md_any, bias_md_any,
                dst_layer_md_any, dst_iter_md_any);
        auto ref_prim_desc = rnn_forward::primitive_desc(ref_desc, eng);

        // Query the descriptor for memory descriptors
        auto weights_layer_md_ref = ref_prim_desc.weights_layer_primitive_desc().desc();
        auto weights_iter_md_ref = ref_prim_desc.weights_iter_primitive_desc().desc();
        auto bias_md_ref = ref_prim_desc.bias_primitive_desc().desc();
        auto src_layer_md_ref = ref_prim_desc.src_layer_primitive_desc().desc();
        auto src_iter_md_ref = ref_prim_desc.src_iter_primitive_desc().desc();
        auto dst_layer_md_ref = ref_prim_desc.dst_layer_primitive_desc().desc();
        auto dst_iter_md_ref = ref_prim_desc.dst_iter_primitive_desc().desc();

        auto are_equal_md = [](memory::desc a, memory::desc b, engine eng){
            return memory::primitive_desc(a, eng)
                    == memory::primitive_desc(b, eng);
        };

        bool skip_test =
            are_equal_md(weights_layer_md_ref, weights_layer_md_tgt, eng)
            && are_equal_md(weights_iter_md_ref, weights_iter_md_tgt, eng)
            && are_equal_md(bias_md_ref, bias_md_tgt, eng)
            && are_equal_md(src_layer_md_ref, src_layer_md_tgt, eng)
            && are_equal_md(src_iter_md_ref, src_iter_md_tgt, eng)
            && are_equal_md(dst_layer_md_ref, dst_layer_md_tgt, eng)
            && are_equal_md(dst_iter_md_ref, dst_iter_md_tgt, eng);

        if (skip_test) return;

        /* initialize data */
        auto weights_layer_ref = memory({weights_layer_md_ref, eng});
        auto weights_iter_ref = memory({weights_iter_md_ref, eng});
        auto bias_ref = memory({bias_md_ref, eng});
        auto src_layer_ref = memory({src_layer_md_ref, eng});
        auto src_iter_ref = memory({src_iter_md_ref, eng});
        auto dst_layer_ref = memory({dst_layer_md_ref, eng});
        auto dst_iter_ref = memory({dst_iter_md_ref, eng});

        auto weights_layer_tgt = memory({weights_layer_md_tgt, eng});
        auto weights_iter_tgt = memory({weights_iter_md_tgt, eng});
        auto bias_tgt = memory({bias_md_tgt, eng});
        auto src_layer_tgt = memory({src_layer_md_tgt, eng});
        auto src_iter_tgt = memory({src_iter_md_tgt, eng});
        auto dst_layer_tgt = memory({dst_layer_md_tgt, eng});
        auto dst_iter_tgt = memory({dst_iter_md_tgt, eng});

        // Assumption: b is a plain layout
        auto init_tensor = [&](memory a, memory b) {
            auto b_ptr = static_cast<float *>(b.get_data_handle());
            auto desc = a.get_primitive_desc().desc();
            auto b_dims = desc.data.dims;
            auto b_ndims = desc.data.ndims;
            auto n_elems = std::accumulate(b_dims, b_dims + b_ndims, size_t(1),
                    std::multiplies<float>());
            for(size_t i = 0; i < n_elems; i++)
                b_ptr[i] = i;
            stream(stream::kind::eager).submit({reorder(b, a)}).wait();
        };

        init_tensor(weights_layer_ref, weights_layer_tgt);
        init_tensor(weights_iter_ref, weights_iter_tgt);
        init_tensor(bias_ref, bias_tgt);
        init_tensor(src_layer_ref, src_layer_tgt);
        init_tensor(src_iter_ref, src_iter_tgt);

        // run the non packed version
        auto prim_ref = rnn_forward(ref_prim_desc, src_layer_ref, src_iter_ref,
                weights_layer_ref, weights_iter_ref, bias_ref,
                dst_layer_ref, dst_iter_ref, null_memory(eng));
        stream(stream::kind::eager).submit({prim_ref}).wait();

        // run the packed version
        rnn_forward::desc tgt_desc(prop_kind::forward_inference, cell,
                direction, src_layer_md_tgt, src_iter_md_tgt,
                weights_layer_md_tgt, weights_iter_md_tgt, bias_md_tgt,
                dst_layer_md_tgt, dst_iter_md_tgt);
        auto tgt_prim_desc = rnn_forward::primitive_desc(tgt_desc, eng);
        auto prim_tgt = rnn_forward(tgt_prim_desc, src_layer_tgt, src_iter_tgt,
                weights_layer_tgt, weights_iter_tgt, bias_tgt,
                dst_layer_tgt, dst_iter_tgt, null_memory(eng));
        stream(stream::kind::eager).submit({prim_tgt}).wait();

        // compare dst_layer and dst_iter
        compare_data<data_t>(dst_layer_ref, dst_layer_tgt, 1e-5);
        compare_data<data_t>(dst_iter_ref, dst_iter_tgt, 1e-5);
    }
};

    using eng = engine::kind;
    using fmt = memory::format;
    using alg = algorithm;
    using dir = rnn_direction;
    using rnn_forward_test_f32 = rnn_forward_test<float>;
    using cfg_f32 = test_rnn_params_t;

TEST_P(rnn_forward_test_f32, TestsRnn) { }
INSTANTIATE_TEST_CASE_P(TestRnn, rnn_forward_test_f32,
        ::testing::Values(
            cfg_f32{eng::cpu, alg::vanilla_rnn, alg::eltwise_tanh, dir::unidirectional_left2right,
                {fmt::tnc, fmt::ldsnc, fmt::ldigo, fmt::ldigo, fmt::ldgo, fmt::tnc, fmt::ldsnc},
                    test_rnn_sizes_t(1, 1, 10, 16, 100, 100, 100, 100)},
            cfg_f32{eng::cpu, alg::vanilla_lstm, alg::eltwise_tanh, dir::unidirectional_left2right,
                {fmt::tnc, fmt::ldsnc, fmt::ldigo, fmt::ldigo, fmt::ldgo, fmt::tnc, fmt::ldsnc},
                    test_rnn_sizes_t(1, 1, 10, 16, 100, 100, 100, 100)},
            /* Check for invalid parameters: unsupported unrolling */
            cfg_f32{eng::cpu, alg::vanilla_rnn, alg::eltwise_tanh, dir::unidirectional_left2right,
                {fmt::tnc, fmt::ldsnc, fmt::ldigo, fmt::ldigo, fmt::ldgo, fmt::tnc, fmt::ldsnc},
                    test_rnn_sizes_t(2, 1, 10, 16, 200, 100, 100, 100), true, mkldnn_invalid_arguments},
            cfg_f32{eng::cpu, alg::vanilla_rnn, alg::eltwise_tanh, dir::unidirectional_left2right,
                {fmt::tnc, fmt::ldsnc, fmt::ldigo, fmt::ldigo, fmt::ldgo, fmt::tnc, fmt::ldsnc},
                    test_rnn_sizes_t(2, 1, 10, 16, 100, 200, 100, 100), true, mkldnn_invalid_arguments},
            /* Check for invalid parameters: inconsistent dimensions */
            cfg_f32{eng::cpu, alg::vanilla_rnn, alg::eltwise_tanh, dir::unidirectional_left2right,
                {fmt::tnc, fmt::ldsnc, fmt::ldigo, fmt::ldigo, fmt::ldgo, fmt::tnc, fmt::ldsnc},
                    test_rnn_sizes_t(2, 1, 10, 16, 100, 100, 50, 100), true, mkldnn_invalid_arguments}
            )
    );

}
