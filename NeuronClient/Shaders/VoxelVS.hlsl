// One quad per surface voxel, drawn as a 4-vertex triangle strip per instance (Voxel.hlsli).

#include "Voxel.hlsli"

VoxelVertexOut main(uint _vertexId : SV_VertexID, uint _instanceId : SV_InstanceID)
{
  return VoxelVertex(_vertexId, _instanceId);
}
