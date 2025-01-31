/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
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

#include "mkldnn_thread.hpp"
#include "simple_sum.hpp"

#include "jit_avx512_core_bf16cvt.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

template <data_type_t src_data_type, data_type_t dst_data_type>
void simple_sum_t<src_data_type, dst_data_type>::execute() const {
    auto output = reinterpret_cast<dst_data_t *>(this->memory());
    const int num_arrs = pd()->n_inputs();
    const memory_desc_wrapper o_d(pd()->dst_pd());
    output += o_d.blk_off(0);
    const src_data_t *input_ptrs[max_num_arrs];

    for (int a = 0; a < num_arrs; ++a) {
        const memory_desc_wrapper i_d(pd()->src_pd(a));

        input_ptrs[a] = reinterpret_cast<const src_data_t *>(
                this->input_memory(a)) + i_d.blk_off(0);
    }

    const size_t nelems = pd()->nelems_;
    const size_t block_size = pd()->block_size_;
    const size_t blocks_number = pd()->blocks_number_;
    const size_t tail = pd()->tail_;

    const auto &scales = pd()->scales_;

    auto sum_block_bf16 = [&](size_t start, size_t end, int ithr) {
        using namespace bf16_cvt_utils;
        const bool is_dst_bf16 = dst_data_type == data_type::bf16;

        const auto bf16_p = pd()->bf16_p_;
        const auto scratchpad = this->scratchpad();
        acc_data_t *wspace = scratchpad.template get<acc_data_t>(
                memory_tracking::names::key_sum_bf16cvt);
        acc_data_t *my_ws = &wspace[ithr * bf16_p.ws_elements_per_thread_];

        for (size_t b = start; b < end; b += bf16_p.acc_loop_step_) {
            acc_data_t *my_acc = is_dst_bf16
                                 ? &my_ws[bf16_p.ws_cvt_elements_per_thread_]
                                 : (acc_data_t *)&output[b];
            size_t current_block = nstl::min(bf16_p.acc_loop_step_, end - b);
            cvt_bfloat16_to_float(my_ws,
                    (mkldnn_bfloat16_t *)&input_ptrs[0][b], current_block);
            for (size_t e = 0; e < current_block; e++)
                my_acc[e] = scales[0] * my_ws[e];

            for (int a = 1; a < num_arrs; a++) {
                cvt_bfloat16_to_float(my_ws,
                        (mkldnn_bfloat16_t *)&input_ptrs[a][b], current_block);
                for (size_t e = 0; e < current_block; e++)
                    my_acc[e] += scales[a] * my_ws[e];
            }
            if (is_dst_bf16)
                cvt_float_to_bfloat16((mkldnn_bfloat16_t *)&output[b],
                    my_acc, current_block);
        }
    };

    auto sum_block = [&](size_t start, size_t end, int ithr) {
        PRAGMA_OMP_SIMD()
        for (size_t e = start; e < end; e++) {
            output[e] = dst_data_t(scales[0] * input_ptrs[0][e]);
        }
        for (int a = 1; a < num_arrs; a++) {
            PRAGMA_OMP_SIMD()
            for (size_t e = start; e < end; e++) {
                output[e] += dst_data_t(scales[a] * input_ptrs[a][e]);
            }
        }
    };

    parallel(0, [&](const int ithr, const int nthr) {
        size_t start{0}, end{0};
        balance211(blocks_number, nthr, ithr, start, end);

        for (size_t nb = start; nb < end; ++nb) {
            size_t start_e = nb * block_size;
            size_t end_e = start_e + block_size;
            if (src_data_type == data_type::bf16)
                sum_block_bf16(start_e, end_e, ithr);
            else
                sum_block(start_e, end_e, ithr);
        }

        if (tail != 0 && ithr == nthr - 1) {
            size_t start_e = nelems - tail;
            size_t end_e = nelems;
            if (src_data_type == data_type::bf16)
                sum_block_bf16(start_e, end_e, ithr);
            else
                sum_block(start_e, end_e, ithr);
        }
    });
}

template struct simple_sum_t<data_type::f32, data_type::f32>;
template struct simple_sum_t<data_type::bf16, data_type::bf16>;
template struct simple_sum_t<data_type::bf16, data_type::f32>;

}
}
}
