// GPU skin cache: linear-blend skinning of one mesh into a per-entity vertex buffer.
//
// This deliberately writes a real buffer instead of skinning in the vertex shader. A ray query traverses
// a BLAS built from vertex MEMORY, so a vertex shader's output is invisible to it -- RT shadows, GI and
// reflections would all see the bind pose while the raster image showed the animation. Unreal requires
// its Skin Cache for ray-traced skeletal meshes for exactly this reason; the BLAS refit reads the same
// buffer this pass writes.
//
// BYTE-ADDRESSED on purpose. The engine's Vertex is a tightly packed vertex-buffer layout (float3 at
// offset 0, float3 at 12, float2 at 24, float4 at 32), and a vertex buffer has no alignment rule -- the
// attribute fetcher reads each attribute on its own. A storage buffer does: declaring the same struct as
// StructuredBuffer<Vertex> puts a float3 at offset 12, which straddles a 16-byte boundary and is invalid
// without the scalarBlockLayout feature (spirv-val rejects it). Reading raw bytes sidesteps the layout
// rules entirely and costs nothing -- a skin cache does not care about the struct, only about offsets.
//
// One thread per vertex. Set 0 = {bind-pose vertices, skin bindings, bone matrices, output vertices,
// params}; nothing else -- no G-buffer, no TLAS, no bindless.

// Offsets into the engine's Vertex (Mesh.hpp). A mismatch here silently shears every skinned mesh, so
// they are spelled out rather than derived.
static const uint kVertexStride = 48; // float3 Position + float3 Normal + float2 TexCoord + float4 Tangent
static const uint kOffsetPosition = 0;
static const uint kOffsetNormal = 12;
static const uint kOffsetTexCoord = 24;
static const uint kOffsetTangent = 32;

// SkinnedVertexWeights (SkinnedMeshImporter.hpp): uint4 indices then float4 weights.
static const uint kSkinStride = 32;
static const uint kOffsetBoneIndices = 0;
static const uint kOffsetBoneWeights = 16;

static const uint kMatrixStride = 64;

ByteAddressBuffer BindPoseVertices : register(t0, space0);
ByteAddressBuffer SkinBindings : register(t1, space0);
ByteAddressBuffer BoneMatrices : register(t2, space0); // model-space pose * inverse bind
RWByteAddressBuffer SkinnedVertices : register(u3, space0);

cbuffer SkinningCB : register(b4, space0)
{
	uint VertexCount;
	uint BoneCount;
	uint2 _Pad;
};

// One bone matrix. The four consecutive 16-byte groups are loaded as the matrix's ROWS, which is what a
// float4x4 read out of a buffer means under -Zpr (the engine compiles every shader with it) -- so
// mul(vector, matrix) below behaves exactly as it would with a typed load.
float4x4 LoadBoneMatrix(uint boneIndex)
{
	const uint base = boneIndex * kMatrixStride;
	return float4x4(asfloat(BoneMatrices.Load4(base + 0)),
	                asfloat(BoneMatrices.Load4(base + 16)),
	                asfloat(BoneMatrices.Load4(base + 32)),
	                asfloat(BoneMatrices.Load4(base + 48)));
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const uint vertexIndex = id.x;
	if (vertexIndex >= VertexCount)
	{
		return;
	}

	const uint vertexBase = vertexIndex * kVertexStride;
	const float3 position = asfloat(BindPoseVertices.Load3(vertexBase + kOffsetPosition));
	const float3 normal = asfloat(BindPoseVertices.Load3(vertexBase + kOffsetNormal));
	const uint2 texCoord = BindPoseVertices.Load2(vertexBase + kOffsetTexCoord); // passed through untouched
	const float4 tangent = asfloat(BindPoseVertices.Load4(vertexBase + kOffsetTangent));

	const uint skinBase = vertexIndex * kSkinStride;
	const uint4 boneIndices = SkinBindings.Load4(skinBase + kOffsetBoneIndices);
	const float4 boneWeights = asfloat(SkinBindings.Load4(skinBase + kOffsetBoneWeights));

	// Blend the four influences into ONE matrix, then transform once. Transforming four times and
	// averaging gives the same position but pays four times over for the normal and tangent as well.
	float4x4 skin = (float4x4)0;
	float totalWeight = 0.0;
	[unroll] for (uint i = 0; i < 4; ++i)
	{
		const float weight = boneWeights[i];
		if (weight <= 0.0 || boneIndices[i] >= BoneCount)
		{
			continue; // no influence, or a binding that outlived its skeleton
		}
		skin += LoadBoneMatrix(boneIndices[i]) * weight;
		totalWeight += weight;
	}

	float3 outPosition = position;
	float3 outNormal = normal;
	float4 outTangent = tangent;
	if (totalWeight > 1e-5)
	{
		// Weights are normalized at import, but a clamped or out-of-range influence can still leave them
		// short; renormalizing keeps such a vertex at full size instead of collapsing it towards the origin.
		skin /= totalWeight;

		outPosition = mul(float4(position, 1.0), skin).xyz;
		// Directions ignore translation. Non-uniform bone scale would want the inverse-transpose, but a
		// per-vertex inverse is the wrong trade for a case this rare; the error is a slightly off normal
		// on a squashed bone.
		const float3x3 skin3 = (float3x3)skin;
		outNormal = normalize(mul(normal, skin3));
		outTangent = float4(normalize(mul(tangent.xyz, skin3)), tangent.w);
	}

	SkinnedVertices.Store3(vertexBase + kOffsetPosition, asuint(outPosition));
	SkinnedVertices.Store3(vertexBase + kOffsetNormal, asuint(outNormal));
	SkinnedVertices.Store2(vertexBase + kOffsetTexCoord, texCoord);
	SkinnedVertices.Store4(vertexBase + kOffsetTangent, asuint(outTangent));
}
