/**
 * This code is part of Qiskit.
 *
 * (C) Copyright IBM 2018, 2019, 2020.
 *
 * This code is licensed under the Apache License, Version 2.0. You may
 * obtain a copy of this license in the LICENSE.txt file in the root directory
 * of this source tree or at http://www.apache.org/licenses/LICENSE-2.0.
 *
 * Any modifications or derivative works of this code must retain this
 * copyright notice, and modified files need to carry a notice indicating
 * that they have been altered from the originals.
 */


#ifndef _qv_host_chunk_container_hpp_
#define _qv_host_chunk_container_hpp_

#include "simulators/statevector/chunk/chunk_container.hpp"


namespace AER {
namespace QV {


//============================================================================
// host chunk container class
//============================================================================
template <typename data_t>
class HostChunkContainer : public ChunkContainer<data_t>
{
protected:
  AERHostVector<thrust::complex<data_t>>  data_;     //host vector for chunks + buffers
  std::vector<thrust::complex<double>*> matrix_;     //pointer to matrix
  std::vector<uint_t*> params_;                      //pointer to additional parameters
public:
  HostChunkContainer()
  {
  }
  ~HostChunkContainer();

  uint_t size(void)
  {
    return data_.size();
  }

  AERHostVector<thrust::complex<data_t>>& vector(void)
  {
    return data_;
  }

  thrust::complex<data_t>& operator[](uint_t i)
  {
    return data_[i];
  }

  uint_t Allocate(int idev,int bits,uint_t chunks,uint_t buffers,uint_t checkpoint);
  void Deallocate(void);
  uint_t Resize(uint_t chunks,uint_t buffers,uint_t checkpoint);

  void StoreMatrix(const std::vector<std::complex<double>>& mat,uint_t iChunk)
  {
    matrix_[iChunk] = (thrust::complex<double>*)&mat[0];
  }
  void StoreUintParams(const std::vector<uint_t>& prm,uint_t iChunk)
  {
    params_[iChunk] = (uint_t*)&prm[0];
  }

  void Set(uint_t i,const thrust::complex<data_t>& t)
  {
    data_[i] = t;
  }
  thrust::complex<data_t> Get(uint_t i) const
  {
    return data_[i];
  }

  thrust::complex<data_t>* chunk_pointer(uint_t iChunk) const
  {
    return (thrust::complex<data_t>*)thrust::raw_pointer_cast(data_.data()) + (iChunk << this->chunk_bits_);
  }

  thrust::complex<double>* matrix_pointer(uint_t iChunk) const
  {
    return matrix_[iChunk];
  }

  uint_t* param_pointer(uint_t iChunk) const
  {
    return params_[iChunk];
  }

  bool peer_access(int i_dest)
  {
#ifdef AER_ATS
    //for IBM AC922
    return true;
#else
    return false;
#endif
  }

  void CopyIn(std::shared_ptr<Chunk<data_t>> src,uint_t iChunk);
  void CopyOut(std::shared_ptr<Chunk<data_t>> src,uint_t iChunk);
  void CopyIn(thrust::complex<data_t>* src,uint_t iChunk);
  void CopyOut(thrust::complex<data_t>* dest,uint_t iChunk);
  void Swap(std::shared_ptr<Chunk<data_t>> src,uint_t iChunk);

  void Zero(uint_t iChunk,uint_t count);

  reg_t sample_measure(uint_t iChunk,const std::vector<double> &rnds, uint_t stride = 1, bool dot = true) const;
  thrust::complex<double> norm(uint_t iChunk,uint_t stride = 1,bool dot = true) const;

};

template <typename data_t>
HostChunkContainer<data_t>::~HostChunkContainer(void)
{
  Deallocate();
}

template <typename data_t>
uint_t HostChunkContainer<data_t>::Allocate(int idev,int bits,uint_t chunks,uint_t buffers,uint_t checkpoint)
{
  uint_t nc = chunks;
  uint_t i;

  ChunkContainer<data_t>::chunk_bits_ = bits;

  ChunkContainer<data_t>::num_buffers_ = buffers;
  ChunkContainer<data_t>::num_checkpoint_ = checkpoint;
  ChunkContainer<data_t>::num_chunks_ = nc;
  data_.resize((nc + buffers + checkpoint) << bits);
  matrix_.resize(nc + buffers);
  params_.resize(nc + buffers);

  //allocate chunk classes
  ChunkContainer<data_t>::allocate_chunks();

  return nc;
}

template <typename data_t>
uint_t HostChunkContainer<data_t>::Resize(uint_t chunks,uint_t buffers,uint_t checkpoint)
{
  uint_t i;

  if(chunks + buffers + checkpoint > this->num_chunks_ + this->num_buffers_ + this->num_checkpoint_){
    data_.resize((chunks + buffers + checkpoint) << this->chunk_bits_);
    matrix_.resize(chunks + buffers);
    params_.resize(chunks + buffers);
  }

  this->num_chunks_ = chunks;
  this->num_buffers_ = buffers;
  this->num_checkpoint_ = checkpoint;

  //allocate chunk classes
  ChunkContainer<data_t>::allocate_chunks();

  return chunks + buffers + checkpoint;
}

template <typename data_t>
void HostChunkContainer<data_t>::Deallocate(void)
{
  data_.clear();
  matrix_.clear();
  params_.clear();
}


template <typename data_t>
void HostChunkContainer<data_t>::CopyIn(std::shared_ptr<Chunk<data_t>> src,uint_t iChunk)
{
  uint_t size = 1ull << this->chunk_bits_;

  if(src->device() >= 0){
    src->set_device();
    auto src_cont = std::static_pointer_cast<DeviceChunkContainer<data_t>>(src->container());
    thrust::copy_n(src_cont->vector().begin() + (src->pos() << this->chunk_bits_),size,data_.begin() + (iChunk << this->chunk_bits_));
  }
  else{
    auto src_cont = std::static_pointer_cast<HostChunkContainer<data_t>>(src->container());

    if(omp_get_num_threads() > 1){  //in parallel region
      thrust::copy_n(src_cont->vector().begin() + (src->pos() << this->chunk_bits_),size,data_.begin() + (iChunk << this->chunk_bits_));
    }
    else{
#pragma omp parallel
      {
        int nid = omp_get_num_threads();
        int tid = omp_get_thread_num();
        uint_t is,ie;

        is = (uint_t)(tid) * size / (uint_t)(nid);
        ie = (uint_t)(tid + 1) * size / (uint_t)(nid);

        thrust::copy_n(src_cont->vector().begin() + (src->pos() << this->chunk_bits_) + is,ie - is,data_.begin() + (iChunk << this->chunk_bits_) + is);
      }
    }
  }
}

template <typename data_t>
void HostChunkContainer<data_t>::CopyOut(std::shared_ptr<Chunk<data_t>> dest,uint_t iChunk)
{
  uint_t size = 1ull << this->chunk_bits_;
  if(dest->device() >= 0){
    dest->set_device();
    auto dest_cont = std::static_pointer_cast<DeviceChunkContainer<data_t>>(dest->container());
    thrust::copy_n(data_.begin() + (iChunk << this->chunk_bits_),size,dest_cont->vector().begin() + (dest->pos() << this->chunk_bits_));
  }
  else{
    auto dest_cont = std::static_pointer_cast<HostChunkContainer<data_t>>(dest->container());

    if(omp_get_num_threads() > 1){  //in parallel region
      thrust::copy_n(data_.begin() + (iChunk << this->chunk_bits_),size,dest_cont->vector().begin() + (dest->pos() << this->chunk_bits_));
    }
    else{
#pragma omp parallel
      {
        int nid = omp_get_num_threads();
        int tid = omp_get_thread_num();
        uint_t is,ie;

        is = (uint_t)(tid) * size / (uint_t)(nid);
        ie = (uint_t)(tid + 1) * size / (uint_t)(nid);
        thrust::copy_n(data_.begin() + (iChunk << this->chunk_bits_)+is,ie-is,dest_cont->vector().begin() + (dest->pos() << this->chunk_bits_)+is);
      }
    }
  }
}

template <typename data_t>
void HostChunkContainer<data_t>::CopyIn(thrust::complex<data_t>* src,uint_t iChunk)
{
  uint_t size = 1ull << this->chunk_bits_;

  thrust::copy_n(src,size,data_.begin() + (iChunk << this->chunk_bits_));
}

template <typename data_t>
void HostChunkContainer<data_t>::CopyOut(thrust::complex<data_t>* dest,uint_t iChunk)
{
  uint_t size = 1ull << this->chunk_bits_;
  thrust::copy_n(data_.begin() + (iChunk << this->chunk_bits_),size,dest);
}

template <typename data_t>
void HostChunkContainer<data_t>::Swap(std::shared_ptr<Chunk<data_t>> src,uint_t iChunk)
{
  uint_t size = 1ull << this->chunk_bits_;
  if(src->device() >= 0){
    src->set_device();

    AERHostVector<thrust::complex<data_t>> tmp1(size);
    auto src_cont = std::static_pointer_cast<DeviceChunkContainer<data_t>>(src->container());
    
    thrust::copy_n(thrust::omp::par,data_.begin() + (iChunk << this->chunk_bits_),size,tmp1.begin());

    thrust::copy_n(src_cont->vector().begin() + (src->pos() << this->chunk_bits_),size,data_.begin() + (iChunk << this->chunk_bits_));
    thrust::copy_n(tmp1.begin(),size,src_cont->vector().begin() + (src->pos() << this->chunk_bits_));
  }
  else{
    auto src_cont = std::static_pointer_cast<HostChunkContainer<data_t>>(src->container());

    if(omp_get_num_threads() > 1){  //in parallel region
      thrust::swap_ranges(thrust::host,data_.begin() + (iChunk << this->chunk_bits_),data_.begin() + (iChunk << this->chunk_bits_) + size,src_cont->vector().begin() + (src->pos() << this->chunk_bits_));
    }
    else{
#pragma omp parallel
      {
        int nid = omp_get_num_threads();
        int tid = omp_get_thread_num();
        uint_t is,ie;

        is = (uint_t)(tid) * size / (uint_t)(nid);
        ie = (uint_t)(tid + 1) * size / (uint_t)(nid);
        thrust::swap_ranges(thrust::host,data_.begin() + (iChunk << this->chunk_bits_) + is,data_.begin() + (iChunk << this->chunk_bits_) + ie,src_cont->vector().begin() + (src->pos() << this->chunk_bits_) + is);
      }
    }
  }
}


template <typename data_t>
void HostChunkContainer<data_t>::Zero(uint_t iChunk,uint_t count)
{
  if(omp_get_num_threads() > 1){  //in parallel region
    thrust::fill_n(thrust::host,data_.begin() + (iChunk << this->chunk_bits_),count,0.0);
  }
  else{
#pragma omp parallel
    {
      int nid = omp_get_num_threads();
      int tid = omp_get_thread_num();
      uint_t is,ie;

      is = (uint_t)(tid) * count / (uint_t)(nid);
      ie = (uint_t)(tid + 1) * count / (uint_t)(nid);
      thrust::fill_n(thrust::host,data_.begin() + (iChunk << this->chunk_bits_) + is,ie-is,0.0);
    }
  }
}

template <typename data_t>
reg_t HostChunkContainer<data_t>::sample_measure(uint_t iChunk,const std::vector<double> &rnds, uint_t stride, bool dot) const
{
  const int_t SHOTS = rnds.size();
  reg_t samples(SHOTS,0);
  thrust::host_vector<uint_t> vSmp(SHOTS);
  int i;

  strided_range<thrust::complex<data_t>*> iter(chunk_pointer(iChunk), chunk_pointer(iChunk+1), stride);

  if(omp_get_num_threads() == 1){
    if(dot)
      thrust::transform_inclusive_scan(thrust::omp::par,iter.begin(),iter.end(),iter.begin(),complex_dot_scan<data_t>(),thrust::plus<thrust::complex<data_t>>());
    else
      thrust::inclusive_scan(thrust::omp::par,iter.begin(),iter.end(),iter.begin(),thrust::plus<thrust::complex<data_t>>());
    thrust::lower_bound(thrust::omp::par, iter.begin(), iter.end(), rnds.begin(), rnds.begin() + SHOTS, vSmp.begin() ,complex_less<data_t>());
  }
  else{
    if(dot)
      thrust::transform_inclusive_scan(thrust::seq,iter.begin(),iter.end(),iter.begin(),complex_dot_scan<data_t>(),thrust::plus<thrust::complex<data_t>>());
    else
      thrust::inclusive_scan(thrust::seq,iter.begin(),iter.end(),iter.begin(),thrust::plus<thrust::complex<data_t>>());
    thrust::lower_bound(thrust::seq, iter.begin(), iter.end(), rnds.begin(), rnds.begin() + SHOTS, vSmp.begin() ,complex_less<data_t>());
  }

  for(i=0;i<SHOTS;i++){
    samples[i] = vSmp[i];
  }
  vSmp.clear();

  return samples;
}

template <typename data_t>
thrust::complex<double> HostChunkContainer<data_t>::norm(uint_t iChunk, uint_t stride, bool dot) const
{
  thrust::complex<double> sum,zero(0.0,0.0);

  strided_range<thrust::complex<data_t>*> iter(chunk_pointer(iChunk), chunk_pointer(iChunk+1), stride);

  if(omp_get_num_threads() == 1){
    if(dot)
      sum = thrust::transform_reduce(thrust::omp::par, iter.begin(),iter.end(),complex_norm<data_t>() ,zero,thrust::plus<thrust::complex<double>>());
    else
      sum = thrust::reduce(thrust::omp::par, iter.begin(),iter.end(),zero,thrust::plus<thrust::complex<double>>());
  }
  else{
    if(dot)
      sum = thrust::transform_reduce(thrust::seq, iter.begin(),iter.end(),complex_norm<data_t>() ,zero,thrust::plus<thrust::complex<double>>());
    else
      sum = thrust::reduce(thrust::seq, iter.begin(),iter.end(),zero,thrust::plus<thrust::complex<double>>());
  }

  return sum;
}

//------------------------------------------------------------------------------
} // end namespace QV
} // end namespace AER
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
#endif // end module
