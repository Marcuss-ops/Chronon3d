#include <cuda_runtime.h>

__global__ void chronon_wbh_smoke_kernel() {}

int main() {
    chronon_wbh_smoke_kernel<<<1, 1>>>();
    const cudaError_t launch = cudaGetLastError();
    if (launch != cudaSuccess) return 2;
    return cudaDeviceSynchronize() == cudaSuccess ? 0 : 1;
}
