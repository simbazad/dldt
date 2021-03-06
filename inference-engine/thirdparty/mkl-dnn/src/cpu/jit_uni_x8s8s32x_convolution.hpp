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

#ifndef CPU_JIT_UNI_X8S8S32X_CONVOLUTION_HPP
#define CPU_JIT_UNI_X8S8S32X_CONVOLUTION_HPP

#include "c_types_map.hpp"
#include "cpu_convolution_pd.hpp"
#include "cpu_engine.hpp"
#include "cpu_reducer.hpp"
#include "jit_primitive_conf.hpp"
#include "jit_uni_x8s8s32x_conv_kernel.hpp"
#include "jit_generator.hpp"
#include "mkldnn_thread.hpp"


namespace mkldnn {
namespace impl {
namespace cpu {

template <cpu_isa_t isa, bool with_relu, impl::data_type_t src_type, impl::data_type_t dst_type>
struct _jit_uni_x8s8s32x_convolution_fwd_t: public cpu_primitive_t {
    struct pd_t: public _cpu_convolution_fwd_pd_t<with_relu> {
        pd_t(engine_t *engine,
                const typename pd_t::base_desc_t *adesc,
                const primitive_attr_t *attr,
                const typename pd_t::base_class *hint_fwd_pd)
            : _cpu_convolution_fwd_pd_t<with_relu>(engine, adesc, attr,
                    hint_fwd_pd)
            , jcp_({}) {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit:", isa, ""),
                _jit_uni_x8s8s32x_convolution_fwd_t<isa, with_relu, src_type, dst_type>);

        virtual status_t init() override {
            using namespace prop_kind;
            assert(this->engine()->kind() == engine_kind::cpu);
            bool ok = true
                && utils::one_of(this->cdesc_().prop_kind, forward_training,
                        forward_inference)
                && this->cdesc_().alg_kind == alg_kind::convolution_direct
                && IMPLICATION(this->with_bias(), utils::one_of(
                    this->cdesc_().bias_desc.data_type, data_type::f32,
                    data_type::s32, data_type::s8, data_type::u8))
                && this->cdesc_().accum_data_type == data_type::s32
                && this->cdesc_().src_desc.data_type == src_type
                && this->cdesc_().dst_desc.data_type == dst_type;
            if (!ok) return status::unimplemented;

            return jit_uni_x8s8s32x_conv_fwd_kernel<isa>::init_conf(jcp_, this->cdesc_(),
                    this->src_pd_, this->weights_pd_,
                    this->dst_pd_, this->bias_pd_, *this->attr(),
                    with_relu, this->negative_slope());
        }

        jit_conv_conf_t jcp_;
    };

    _jit_uni_x8s8s32x_convolution_fwd_t(const pd_t *pd, const input_vector &inputs,
            const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd), local_scales_(nullptr) {
        kernel_ = new jit_uni_x8s8s32x_conv_fwd_kernel<isa>(conf_.jcp_, *conf_.attr());

        if (conf_.jcp_.signed_input) {
            size_t scales_size = (conf_.attr()->output_scales_.count_ == 1)
                                 ? 8
                                 : conf_.attr()->output_scales_.count_;
            local_scales_ = (float *)malloc(sizeof(float) * scales_size, 64);
            for (size_t i = 0; i < scales_size; i++) {
                local_scales_[i] = conf_.attr()->output_scales_.scales_[i] *
                                   (1.0 / conf_.jcp_.wei_adj_scale);
            }
        }
    }

    ~_jit_uni_x8s8s32x_convolution_fwd_t() {
        delete kernel_;
        if (local_scales_) free(local_scales_);
    };

    typedef typename prec_traits<data_type::u8>::type src_data_t;
    typedef typename prec_traits<data_type::s8>::type wei_data_t;
    typedef typename prec_traits<dst_type>::type dst_data_t;

    virtual void execute(event_t *e) {
        execute_forward();
        e->set_state(event_t::ready);
    }

private:
    void execute_forward();
    pd_t conf_;
    jit_uni_x8s8s32x_conv_fwd_kernel<isa> *kernel_;
    float *local_scales_;
};

template <impl::data_type_t src_type, impl::data_type_t dst_type>
using jit_avx2_x8s8s32x_convolution_fwd_t = _jit_uni_x8s8s32x_convolution_fwd_t<avx2, false, src_type, dst_type>;

template <impl::data_type_t src_type, impl::data_type_t dst_type>
using jit_avx2_x8s8s32x_convolution_relu_t = _jit_uni_x8s8s32x_convolution_fwd_t<avx2, true, src_type, dst_type>;

template <impl::data_type_t src_type, impl::data_type_t dst_type>
using jit_sse42_x8s8s32x_convolution_fwd_t = _jit_uni_x8s8s32x_convolution_fwd_t<sse42, false, src_type, dst_type>;

template <impl::data_type_t src_type, impl::data_type_t dst_type>
using jit_sse42_x8s8s32x_convolution_relu_t = _jit_uni_x8s8s32x_convolution_fwd_t<sse42, true, src_type, dst_type>;

}
}
}

#endif
