#ifndef PRIMITIV_TENSOR_H_
#define PRIMITIV_TENSOR_H_

#include <vector>
#include <primitiv/shape.h>

namespace primitiv {

class Device;

/**
 * Value with any dimensions.
 */
class Tensor {
  Tensor(const Tensor &) = delete;
  Tensor &operator=(const Tensor &) = delete;

public:
  Tensor(Tensor &&);
  Tensor &operator=(Tensor &&);
  ~Tensor();

  /**
   * Creates an invalid Tensor.
   */
  inline Tensor() : shape_(), device_(nullptr), data_(nullptr) {}

  /**
   * Creates a new uninitialized Tensor.
   * @param shape Shape of the new Tensor.
   * @param device Device object to manage the internal memory.
   * @param data Pointer of the device-specific object.
   * @remarks This constructor should not be used directly by users.
   */
  inline Tensor(const Shape &shape, Device *device, void *data)
    : shape_(shape), device_(device), data_(data) {}

  /**
   * Returns the shape of the Tensor.
   * @return Shape of the Tensor.
   */
  inline const Shape &shape() const { return shape_; }

  /**
   * Returns the Device object related to the internal memory.
   * @return Device object.
   */
  inline Device *device() const { return device_; }

  /**
   * Returns the raw pointer of the internal memory.
   * @return Pointer of the internal memory.
   */
  inline void *data() { return data_; }

  /**
   * Returns the raw const-pointer of the internal memory.
   * @return Const-pointer of the internal memory.
   */
  inline const void *data() const { return data_; }

  /**
   * Retrieves internal values of the tensor as a vector.
   * @return A list of the internal values.
   * @remarks Each resulting values a re ordered by the column-major order, and
   *          the batch size is assumed as the last dimension of the tensor.
   */
  std::vector<float> get_values() const;

  /**
   * Reset internal values using a constant.
   * @param k A value to be used to initialize each element.
   */
  void set_values(const float k);

  /**
   * Reset internal values using a vector.
   * @param values List of values to be used to initialize each element.
   * @remarks `values.size()` should be equal to `shape().size()`. Each element
   *          should be ordered by the column-major order, and the batch size is
   *          assumed as the last dimension.
   */
  void set_values(const std::vector<float> &values);

  /**
   * Check whether the object is valid or not.
   * @return true if the object is valid, false otherwise.
   * @remarks This returns false when the object is created through the default
   *          constructor or the object had been moved.
   */
  inline bool valid() const { return !!data_; }

  /**
   * Adds a tensor for gradient calculation.
   * @param x A tensor to add.
   */
  void add_gradient(const Tensor &x);

private:
  Shape shape_;
  Device *device_;
  void *data_;
};

}  // namespace primitiv

#endif  // PRIMITIV_TENSOR_H_
