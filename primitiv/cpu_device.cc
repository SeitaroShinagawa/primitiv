#include <config.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <primitiv/cpu_device.h>
#include <primitiv/error.h>

using std::cerr;
using std::endl;

namespace primitiv {

CPUDevice::CPUDevice() : blocks_(), rng_(std::random_device()()) {}
CPUDevice::CPUDevice(unsigned rng_seed) : blocks_(), rng_(rng_seed) {}

CPUDevice::~CPUDevice() {
  // check memory leak
  if (!blocks_.empty()) {
    cerr << "FATAL ERROR: Detected memory leak on CPUDevice!" << endl;
    cerr << "Leaked blocks (handle: size):" << endl;
    for (const auto &kv : blocks_) {
      cerr << "  " << kv.first << ": " << kv.second << endl;
    }
    std::abort();
  }
}

void *CPUDevice::new_handle(const Shape &shape) {
  const unsigned mem_size = sizeof(float) * shape.num_total_elements();
  void *data = std::malloc(mem_size);
  if (!data) {
    THROW_ERROR("Memory allocation failed. Requested size: " << mem_size);
  }
  blocks_.insert(std::make_pair(data, mem_size));
  return data;
}

void CPUDevice::delete_tensor_impl(Tensor &x) {
  void *data = x.data();
  auto it = blocks_.find(data);
  if (it == blocks_.end()) {
    THROW_ERROR("Attempted to dispose unknown memory block: " << data);
  }
  blocks_.erase(it);
  std::free(data);
}

#define DATA(x) static_cast<float *>((x).data())
#define CDATA(x) static_cast<const float *>((x).data())

#define REPEAT_OP(i, n, op) \
  for (unsigned (i) = 0; (i) < (n); ++(i)) { (op); }

std::vector<float> CPUDevice::tensor_to_vector_impl(const Tensor &x) {
  const unsigned num_elements = x.shape().num_total_elements();
  std::vector<float> ret(num_elements);
  std::memcpy(&ret[0], x.data(), sizeof(float) * num_elements);
  return ret;
}

void CPUDevice::reset_tensor_impl(Tensor &x, float k) {
  float *dest = DATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = k);
}

void CPUDevice::reset_tensor_impl(Tensor &x, const std::vector<float> &values) {
  std::memcpy(x.data(), &values[0], sizeof(float) * x.shape().num_total_elements());
}

Tensor CPUDevice::random_bernoulli_impl(const Shape &shape, float p) {
  std::bernoulli_distribution dist(p);
  Tensor ret = new_tensor(shape);
  float *dest = DATA(ret);
  const unsigned size = shape.num_total_elements();
  REPEAT_OP(i, size, dest[i] = dist(rng_));
  return ret;
}

Tensor CPUDevice::random_uniform_impl(
    const Shape &shape, float lower, float upper) {
  std::uniform_real_distribution<float> dist(lower, upper);
  Tensor ret = new_tensor(shape);
  float *dest = DATA(ret);
  const unsigned size = shape.num_total_elements();
  for (unsigned i = 0; i < size; ++i) {
    const float x = dist(rng_);
    dest[i] = x == lower ? upper : x;
  }
  return ret;
}

Tensor CPUDevice::random_normal_impl(const Shape &shape, float mean, float sd) {
  std::normal_distribution<float> dist(mean, sd);
  Tensor ret = new_tensor(shape);
  float *dest = DATA(ret);
  const unsigned size = shape.num_total_elements();
  REPEAT_OP(i, size, dest[i] = dist(rng_));
  return ret;
}

Tensor CPUDevice::slice_impl(
    const Tensor &x, unsigned dim, unsigned offset, const Shape &new_shape) {
  const unsigned base = new_shape.num_elements_under_rank(dim);
  const unsigned span = base * new_shape[dim];
  const unsigned skip = base * x.shape()[dim];
  const unsigned repeat = new_shape.num_total_elements() / span;

  Tensor ret = new_tensor(new_shape);
  float *dest = DATA(ret);
  const float *src = CDATA(x) + base * offset;
  for (unsigned i = 0; i < repeat; ++i) {
    const float *sp = src;
    REPEAT_OP(j, span, *dest++ = *sp++);
    src += skip;
  }
  return ret;
}

Tensor CPUDevice::concat_impl(
    const std::vector<const Tensor *> &xs,
    unsigned dim, const Shape &new_shape) {
  const unsigned new_bs = new_shape.batch_size();
  const unsigned base = new_shape.num_elements_under_rank(dim);
  const unsigned skip = base * new_shape[dim];
  const unsigned repeat = new_shape.num_elements_per_sample() / skip;

  Tensor ret = new_tensor(new_shape);
  unsigned offset = 0;
  for (const Tensor *x : xs) {
    const unsigned src_dim = x->shape()[dim];
    const unsigned span = base * src_dim;
    const unsigned b_skip = (x->shape().batch_size() > 1) * span * repeat;
    float *dest = DATA(ret) + offset;
    const float *src = CDATA(*x);
    for (unsigned b = 0; b < new_bs; ++b) {
      const float *sp = src;
      for (unsigned i = 0; i < repeat; ++i) {
        float *dp = dest;
        REPEAT_OP(j, span, *dp++ = *sp++);
        dest += skip;
      }
      src += b_skip;
    }
    offset += span;
  }
  return ret;
}

Tensor CPUDevice::duplicate_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  std::memcpy(ret.data(), x.data(), sizeof(float) * x.shape().num_total_elements());
  return ret;
}

Tensor CPUDevice::negate_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = -src[i]);
  return ret;
}

Tensor CPUDevice::add_impl(const Tensor &x, float k) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = src[i] + k);
  return ret;
}

Tensor CPUDevice::add_impl(const Tensor &a, const Tensor &b) {
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const unsigned size = sa.num_elements_per_sample();
  const unsigned bs = std::max(sa.batch_size(), sb.batch_size());
  const unsigned skip_a = (sa.batch_size() > 1) * size;
  const unsigned skip_b = (sb.batch_size() > 1) * size;
  Tensor ret = new_tensor(sa.resize_batch(bs));
  float *dest = DATA(ret);
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);
  for (unsigned b = 0; b < bs; ++b) {
    REPEAT_OP(i, size, dest[i] = src_a[i] + src_b[i]);
    dest += size;
    src_a += skip_a;
    src_b += skip_b;
  }
  return ret;
}

Tensor CPUDevice::subtract_impl(const Tensor &x, float k) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = src[i] - k);
  return ret;
}

Tensor CPUDevice::subtract_impl(float k, const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = k - src[i]);
  return ret;
}

Tensor CPUDevice::subtract_impl(const Tensor &a, const Tensor &b) {
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const unsigned size = sa.num_elements_per_sample();
  const unsigned bs = std::max(sa.batch_size(), sb.batch_size());
  const unsigned skip_a = (sa.batch_size() > 1) * size;
  const unsigned skip_b = (sb.batch_size() > 1) * size;
  Tensor ret = new_tensor(sa.resize_batch(bs));
  float *dest = DATA(ret);
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);
  for (unsigned b = 0; b < bs; ++b) {
    REPEAT_OP(i, size, dest[i] = src_a[i] - src_b[i]);
    dest += size;
    src_a += skip_a;
    src_b += skip_b;
  }
  return ret;
}

Tensor CPUDevice::multiply_impl(const Tensor &x, float k) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = src[i] * k);
  return ret;
}

Tensor CPUDevice::multiply_impl(const Tensor &a, const Tensor &b) {
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const unsigned size = sa.num_elements_per_sample();
  const unsigned bs = std::max(sa.batch_size(), sb.batch_size());
  const unsigned skip_a = (sa.batch_size() > 1) * size;
  const unsigned skip_b = (sb.batch_size() > 1) * size;
  Tensor ret = new_tensor(sa.resize_batch(bs));
  float *dest = DATA(ret);
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);
  for (unsigned b = 0; b < bs; ++b) {
    REPEAT_OP(i, size, dest[i] = src_a[i] * src_b[i]);
    dest += size;
    src_a += skip_a;
    src_b += skip_b;
  }
  return ret;
}

Tensor CPUDevice::divide_impl(const Tensor &x, float k) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = src[i] / k);
  return ret;
}

Tensor CPUDevice::divide_impl(float k, const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = k / src[i]);
  return ret;
}

Tensor CPUDevice::divide_impl(const Tensor &a, const Tensor &b) {
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const unsigned size = sa.num_elements_per_sample();
  const unsigned bs = std::max(sa.batch_size(), sb.batch_size());
  const unsigned skip_a = (sa.batch_size() > 1) * size;
  const unsigned skip_b = (sb.batch_size() > 1) * size;
  Tensor ret = new_tensor(sa.resize_batch(bs));
  float *dest = DATA(ret);
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);
  for (unsigned b = 0; b < bs; ++b) {
    REPEAT_OP(i, size, dest[i] = src_a[i] / src_b[i]);
    dest += size;
    src_a += skip_a;
    src_b += skip_b;
  }
  return ret;
}

Tensor CPUDevice::transpose_impl(const Tensor &x) {
  const Shape &s = x.shape();
  const unsigned d1 = s[0];
  const unsigned d2 = s[1];
  const unsigned ms = d1 * d2;
  const unsigned bs = s.batch_size();
  Tensor ret = new_tensor(Shape({d2, d1}, bs));
  float *dest = DATA(ret);
  const float *src = CDATA(x);

  for (unsigned k = 0; k < bs; ++k) {
    float *pd = dest;
    for (unsigned j = 0; j < d2; ++j) {
      float *ppd = pd;
      for (unsigned i = 0; i < d1; ++i) {
        *ppd = *src++;
        ppd += d2;
      }
      ++pd;
    }
    dest += ms;
  }

  return ret;
}

Tensor CPUDevice::dot_impl(const Tensor &a, const Tensor &b) {
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const unsigned d1 = sa[0];
  const unsigned d2 = sa[1];
  const unsigned d3 = sb[1];
  const unsigned bs = std::max(sa.batch_size(), sb.batch_size());
  const unsigned dest_shift = d1 * d3;
  const unsigned src_a_shift = (sa.batch_size() > 1) * d1 * d2;
  const unsigned src_b_shift = (sb.batch_size() > 1) * d2 * d3;

  Tensor ret = new_tensor(Shape({d1, d3}, bs));
  float *dest = DATA(ret);
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);
  for (unsigned b = 0; b < bs; ++b) {
    for (unsigned i = 0; i < d1; ++i) {
      for (unsigned ky = 0, kb = 0; ky < dest_shift; ky += d1, kb += d2) {
        float tmp = 0;
        for (unsigned ja = 0, jb = 0; jb < d2; ja += d1, ++jb) {
          tmp += src_a[i + ja] * src_b[jb + kb];
        }
        dest[i + ky] = tmp;
      }
    }
    dest += dest_shift;
    src_a += src_a_shift;
    src_b += src_b_shift;
  }
  return ret;
}

Tensor CPUDevice::exp_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = std::exp(src[i]));
  return ret;
}

Tensor CPUDevice::tanh_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = std::tanh(src[i]));
  return ret;
}

Tensor CPUDevice::sigmoid_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = .5 + .5 * std::tanh(.5 * src[i]));
  return ret;
}

Tensor CPUDevice::step_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = static_cast<float>(src[i] > 0));
  return ret;
}

Tensor CPUDevice::relu_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().num_total_elements();
  REPEAT_OP(i, size, dest[i] = std::max(src[i], .0f));
  return ret;
}

Tensor CPUDevice::sum_impl(const Tensor &x, unsigned dim) {
  const Shape new_shape = x.shape().resize_dim(dim, 1);
  const unsigned n = x.shape()[dim];
  const unsigned repeat = new_shape.num_total_elements();
  const unsigned skip1 = new_shape.num_elements_under_rank(dim);
  const unsigned skip2 = skip1 * n;
  Tensor ret = new_tensor(new_shape);
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  for (unsigned i = 0; i < repeat; ++i) {
    unsigned offset = i % skip1 + (i / skip1) * skip2;
    float tmp = 0;
    for (unsigned j = 0; j < n; ++j) {
      tmp += src[offset];
      offset += skip1;
    }
    dest[i] = tmp;
  }
  return ret;
}

Tensor CPUDevice::broadcast_impl(const Tensor &x, unsigned dim) {
  THROW_ERROR("not implemented");
}

Tensor CPUDevice::batch_sum_impl(const Tensor &x) {
  Tensor ret = new_tensor(x.shape().resize_batch(1));
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned bs = x.shape().batch_size();
  const unsigned size = ret.shape().num_total_elements();
  for (unsigned i = 0; i < size; ++i) {
    float temp = 0;
    for (unsigned b = 0, pos = i; b < bs; ++b, pos += size) {
      temp += src[pos];
    }
    dest[i] = temp;
  }
  return ret;
}

void CPUDevice::add_gradient_impl(Tensor &a, const Tensor &b) {
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const unsigned size = sa.num_elements_per_sample();
  const unsigned bs = std::max(sa.batch_size(), sb.batch_size());
  const unsigned b_skip_d = (sa.batch_size() > 1) * size;
  const unsigned b_skip_s = (sb.batch_size() > 1) * size;
  float *dest = DATA(a);
  const float *src = CDATA(b);
  for (unsigned b = 0; b < bs; ++b) {
    REPEAT_OP(i, size, dest[i] += src[i]);
    dest += b_skip_d;
    src += b_skip_s;
  }
}

void CPUDevice::add_gradient_offset_impl(
    Tensor &a, const Tensor &b, unsigned dim, unsigned offset) {
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const unsigned base = sa.num_elements_under_rank(dim);
  const unsigned span = base * sb[dim];
  const unsigned skip = base * sa[dim];
  const unsigned repeat = sa.num_elements_per_sample() / skip;
  const unsigned bs = std::max(sa.batch_size(), sb.batch_size());
  const unsigned b_skip_d = (sa.batch_size() > 1) * sa.num_elements_per_sample();
  const unsigned b_skip_s = (sb.batch_size() > 1) * sb.num_elements_per_sample();
  float *dest = DATA(a) + base * offset;
  const float *src = CDATA(b);
  for (unsigned b = 0; b < bs; ++b) {
    float *dp = dest;
    const float *sp = src;
    for (unsigned i = 0; i < repeat; ++i) {
      float *ddp = dp;
      REPEAT_OP(j, span, *ddp++ += *sp++);
      dp += skip;
    }
    dest += b_skip_d;
    src += b_skip_s;
  }
}

}  // namespace primitiv
