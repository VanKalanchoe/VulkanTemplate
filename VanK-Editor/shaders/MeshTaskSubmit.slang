#include "shaderIO.h"

//------------------------------------------------------------------------------
// Resource Bindings
//------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64,1,1)]
void compMain(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
}