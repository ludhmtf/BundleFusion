#include "stdafx.h"
#include "SIFTImageManager.h"
#include "../GlobalBundlingState.h"

SIFTImageManager::SIFTImageManager(unsigned int submapSize, unsigned int maxImages /*= 500*/, unsigned int maxKeyPointsPerImage /*= 4096*/)
{
	m_maxNumImages = maxImages;
	m_maxKeyPointsPerImage = maxKeyPointsPerImage;
	m_submapSize = submapSize;

	alloc();
	m_timer = NULL;
}

SIFTImageManager::~SIFTImageManager()
{
	free();
}

SIFTImageGPU& SIFTImageManager::getImageGPU(unsigned int imageIdx)
{
	assert(m_bFinalizedGPUImage);
	return m_SIFTImagesGPU[imageIdx];
}

const SIFTImageGPU& SIFTImageManager::getImageGPU(unsigned int imageIdx) const
{
	assert(m_bFinalizedGPUImage);
	return m_SIFTImagesGPU[imageIdx];
}

unsigned int SIFTImageManager::getNumImages() const
{
	return (unsigned int)m_SIFTImagesGPU.size();
}

unsigned int SIFTImageManager::getNumKeyPointsPerImage(unsigned int imageIdx) const
{
	return m_numKeyPointsPerImage[imageIdx];
}

SIFTImageGPU& SIFTImageManager::createSIFTImageGPU()
{
	assert(m_SIFTImagesGPU.size() == 0 || m_bFinalizedGPUImage);
	assert(m_SIFTImagesGPU.size() < m_maxNumImages);

	unsigned int imageIdx = (unsigned int)m_SIFTImagesGPU.size();
	m_SIFTImagesGPU.push_back(SIFTImageGPU());

	SIFTImageGPU& imageGPU = m_SIFTImagesGPU.back();

	//imageGPU.d_keyPointCounter = d_keyPointCounters + imageIdx;
	imageGPU.d_keyPoints = d_keyPoints + m_numKeyPoints;
	imageGPU.d_keyPointDescs = d_keyPointDescs + m_numKeyPoints;

	m_bFinalizedGPUImage = false;
	return imageGPU;
}

void SIFTImageManager::finalizeSIFTImageGPU(unsigned int numKeyPoints)
{
	assert(numKeyPoints < m_maxKeyPointsPerImage);
	assert(!m_bFinalizedGPUImage);

	m_numKeyPointsPerImagePrefixSum.push_back(m_numKeyPoints);
	m_numKeyPoints += numKeyPoints;
	m_numKeyPointsPerImage.push_back(numKeyPoints);
	m_bFinalizedGPUImage = true;

	assert(getNumImages() == m_numKeyPointsPerImage.size());
	assert(getNumImages() == m_numKeyPointsPerImagePrefixSum.size());
}

ImagePairMatch& SIFTImageManager::getImagePairMatch(unsigned int prevImageIdx, uint2& keyPointOffset)
{
	assert(prevImageIdx < m_maxNumImages);
	assert(getNumImages() > 0);
	keyPointOffset = make_uint2(m_numKeyPointsPerImagePrefixSum[prevImageIdx], m_numKeyPointsPerImagePrefixSum[getNumImages()-1]);
	return m_currImagePairMatches[prevImageIdx];
}






void SIFTImageManager::saveToFile(const std::string& s)
{
	std::ofstream out(s, std::ios::binary);
	if (!out.is_open()) {
		std::cout << "Error opening " << s << " for write" << std::endl;
		return;
	}

	std::vector<SIFTKeyPoint> keyPoints(m_maxNumImages*m_maxKeyPointsPerImage);
	std::vector<SIFTKeyPointDesc> keyPointDescs(m_maxNumImages*m_maxKeyPointsPerImage);
	std::vector<int> keyPointCounters(m_maxNumImages);

	CUDA_SAFE_CALL(cudaMemcpy(keyPoints.data(), d_keyPoints, sizeof(SIFTKeyPoint)*m_maxNumImages*m_maxKeyPointsPerImage, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(keyPointDescs.data(), d_keyPointDescs, sizeof(SIFTKeyPointDesc)*m_maxNumImages*m_maxKeyPointsPerImage, cudaMemcpyDeviceToHost));
	//CUDA_SAFE_CALL(cudaMemcpy(keyPointCounters.data(), d_keyPointCounters, sizeof(int)*m_maxNumImages, cudaMemcpyDeviceToHost));


	const unsigned maxImageMatches = m_maxNumImages;
	std::vector<int> currNumMatchesPerImagePair(maxImageMatches);
	std::vector<float> currMatchDistances(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);
	std::vector<uint2> currMatchKeyPointIndices(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);

	CUDA_SAFE_CALL(cudaMemcpy(currNumMatchesPerImagePair.data(), d_currNumMatchesPerImagePair, sizeof(int)*maxImageMatches, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(currMatchDistances.data(), d_currMatchDistances, sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(currMatchKeyPointIndices.data(), d_currMatchKeyPointIndices, sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyDeviceToHost));


	std::vector<int> currNumFilteredMatchesPerImagePair(maxImageMatches);
	std::vector<float> currFilteredMatchDistances(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
	std::vector<uint2> currFilteredMatchKeyPointIndices(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
	std::vector < float4x4 > currFilteredTransforms(maxImageMatches);
	std::vector<float4x4> currFilteredTransformsInv(maxImageMatches);

	CUDA_SAFE_CALL(cudaMemcpy(currNumFilteredMatchesPerImagePair.data(), d_currNumFilteredMatchesPerImagePair, sizeof(int)*maxImageMatches, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(currFilteredMatchDistances.data(), d_currFilteredMatchDistances, sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(currFilteredMatchKeyPointIndices.data(), d_currFilteredMatchKeyPointIndices, sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(currFilteredTransforms.data(), d_currFilteredTransforms, sizeof(float4x4)*maxImageMatches, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(currFilteredTransformsInv.data(), d_currFilteredTransformsInv, sizeof(float4x4)*maxImageMatches, cudaMemcpyDeviceToHost));

	std::vector<EntryJ> globMatches(m_globNumResiduals);
	std::vector<uint2> globMatchesKeyPointIndices(m_globNumResiduals);
	CUDA_SAFE_CALL(cudaMemcpy(globMatches.data(), d_globMatches, sizeof(EntryJ)*m_globNumResiduals, cudaMemcpyDeviceToHost));
	CUDA_SAFE_CALL(cudaMemcpy(globMatchesKeyPointIndices.data(), d_globMatchesKeyPointIndices, sizeof(uint2)*m_globNumResiduals, cudaMemcpyDeviceToHost));

	const unsigned int numImages = getNumImages();
	out.write((char*)&numImages, sizeof(unsigned int));
	out.write((char*)&m_numKeyPoints, sizeof(unsigned int));
	out.write((char*)m_numKeyPointsPerImage.data(), sizeof(unsigned int)*m_numKeyPointsPerImage.size());
	out.write((char*)m_numKeyPointsPerImagePrefixSum.data(), sizeof(unsigned int)*m_numKeyPointsPerImagePrefixSum.size());

	out.write((char*)keyPoints.data(), sizeof(SIFTKeyPoint)*m_maxNumImages*m_maxKeyPointsPerImage);
	out.write((char*)keyPointDescs.data(), sizeof(SIFTKeyPointDesc)*m_maxNumImages*m_maxKeyPointsPerImage);
	out.write((char*)keyPointCounters.data(), sizeof(int)*m_maxNumImages);

	out.write((char*)currNumMatchesPerImagePair.data(), sizeof(int)*maxImageMatches);
	out.write((char*)currMatchDistances.data(), sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);
	out.write((char*)currMatchKeyPointIndices.data(), sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);

	out.write((char*)currNumFilteredMatchesPerImagePair.data(), sizeof(int)*maxImageMatches);
	out.write((char*)currFilteredMatchDistances.data(), sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
	out.write((char*)currFilteredMatchKeyPointIndices.data(), sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
	out.write((char*)currFilteredTransforms.data(), sizeof(float4x4)*maxImageMatches);
	out.write((char*)currFilteredTransformsInv.data(), sizeof(float4x4)*maxImageMatches);

	out.write((char*)m_validImages.data(), sizeof(int)*m_maxNumImages);

	out.write((char*)&m_globNumResiduals, sizeof(unsigned int));
	if (m_globNumResiduals) {
		out.write((char*)globMatches.data(), sizeof(EntryJ)*m_globNumResiduals);
		out.write((char*)globMatchesKeyPointIndices.data(), sizeof(uint2)*m_globNumResiduals);
	}

	out.write((char*)&m_submapSize, sizeof(unsigned int));

	out.close();
}

void SIFTImageManager::loadFromFile(const std::string& s)
{
	free();
	alloc();

	std::ifstream in(s, std::ios::binary);
	if (!in.is_open()) {
		std::cout << "Error opening " << s << " for read" << std::endl;
		return;
	}

	unsigned int numImages = 0;
	in.read((char*)&numImages, sizeof(unsigned int));
	m_SIFTImagesGPU.resize(numImages);
	m_numKeyPointsPerImage.resize(numImages);
	m_numKeyPointsPerImagePrefixSum.resize(numImages);

	in.read((char*)&m_numKeyPoints, sizeof(unsigned int));
	in.read((char*)m_numKeyPointsPerImage.data(), sizeof(unsigned int)*m_numKeyPointsPerImage.size());
	in.read((char*)m_numKeyPointsPerImagePrefixSum.data(), sizeof(unsigned int)*m_numKeyPointsPerImagePrefixSum.size());

	{
		std::vector<SIFTKeyPoint> keyPoints(m_maxNumImages*m_maxKeyPointsPerImage);
		std::vector<SIFTKeyPointDesc> keyPointDescs(m_maxNumImages*m_maxKeyPointsPerImage);
		std::vector<int> keyPointCounters(m_maxNumImages);

		in.read((char*)keyPoints.data(), sizeof(SIFTKeyPoint)*m_maxNumImages*m_maxKeyPointsPerImage);
		in.read((char*)keyPointDescs.data(), sizeof(SIFTKeyPointDesc)*m_maxNumImages*m_maxKeyPointsPerImage);
		in.read((char*)keyPointCounters.data(), sizeof(int)*m_maxNumImages);

		CUDA_SAFE_CALL(cudaMemcpy(d_keyPoints, keyPoints.data(), sizeof(SIFTKeyPoint)*m_maxNumImages*m_maxKeyPointsPerImage, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL(cudaMemcpy(d_keyPointDescs, keyPointDescs.data(), sizeof(SIFTKeyPointDesc)*m_maxNumImages*m_maxKeyPointsPerImage, cudaMemcpyHostToDevice));
		//CUDA_SAFE_CALL(cudaMemcpy(d_keyPointCounters, keyPointCounters.data(), sizeof(int)*m_maxNumImages, cudaMemcpyHostToDevice));
	}

	{
		const unsigned maxImageMatches = m_maxNumImages;
		std::vector<int> currNumMatchesPerImagePair(maxImageMatches);
		std::vector<float> currMatchDistances(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);
		std::vector<uint2> currMatchKeyPointIndices(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);

		in.read((char*)currNumMatchesPerImagePair.data(), sizeof(int)*maxImageMatches);
		in.read((char*)currMatchDistances.data(), sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);
		in.read((char*)currMatchKeyPointIndices.data(), sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW);

		CUDA_SAFE_CALL(cudaMemcpy(d_currNumMatchesPerImagePair, currNumMatchesPerImagePair.data(), sizeof(int)*maxImageMatches, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL(cudaMemcpy(d_currMatchDistances, currMatchDistances.data(), sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL(cudaMemcpy(d_currMatchKeyPointIndices, currMatchKeyPointIndices.data(), sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW, cudaMemcpyHostToDevice));

		std::vector<int> currNumFilteredMatchesPerImagePair(maxImageMatches);
		std::vector<float> currFilteredMatchDistances(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
		std::vector<uint2> currFilteredMatchKeyPointIndices(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
		std::vector<uint2> currFilteredTransforms(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
		std::vector<uint2> currFilteredTransformsInv(maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);

		in.read((char*)currNumFilteredMatchesPerImagePair.data(), sizeof(int)*maxImageMatches);
		in.read((char*)currFilteredMatchDistances.data(), sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
		in.read((char*)currFilteredMatchKeyPointIndices.data(), sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
		in.read((char*)currFilteredTransforms.data(), sizeof(float4x4)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);
		in.read((char*)currFilteredTransformsInv.data(), sizeof(float4x4)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED);

		CUDA_SAFE_CALL(cudaMemcpy(d_currNumFilteredMatchesPerImagePair, currNumFilteredMatchesPerImagePair.data(), sizeof(int)*maxImageMatches, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL(cudaMemcpy(d_currFilteredMatchDistances, currFilteredMatchDistances.data(), sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL(cudaMemcpy(d_currFilteredMatchKeyPointIndices, currFilteredMatchKeyPointIndices.data(), sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL(cudaMemcpy(d_currFilteredTransforms, currFilteredTransforms.data(), sizeof(float4x4)*maxImageMatches, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL(cudaMemcpy(d_currFilteredTransformsInv, currFilteredTransformsInv.data(), sizeof(float4x4)*maxImageMatches, cudaMemcpyHostToDevice));

		m_validImages.resize(m_maxNumImages);
		in.read((char*)m_validImages.data(), sizeof(int) * m_maxNumImages);
	}

	{
		in.read((char*)&m_globNumResiduals, sizeof(unsigned int));
		if (m_globNumResiduals) {
			std::vector<EntryJ> globMatches(m_globNumResiduals);
			std::vector<uint2> globMatchesKeyPointIndices(m_globNumResiduals);
			in.read((char*)globMatches.data(), sizeof(EntryJ)*m_globNumResiduals);
			in.read((char*)globMatchesKeyPointIndices.data(), sizeof(uint2)*m_globNumResiduals);
			CUDA_SAFE_CALL(cudaMemcpy(d_globNumResiduals, &m_globNumResiduals, sizeof(unsigned int), cudaMemcpyHostToDevice))
			CUDA_SAFE_CALL(cudaMemcpy(d_globMatches, globMatches.data(), sizeof(EntryJ)*m_globNumResiduals, cudaMemcpyHostToDevice));
			CUDA_SAFE_CALL(cudaMemcpy(d_globMatchesKeyPointIndices, globMatchesKeyPointIndices.data(), sizeof(uint2)*m_globNumResiduals, cudaMemcpyHostToDevice));
		}
	}
	{
		in.read((char*)&m_submapSize, sizeof(unsigned int));
	}

	for (unsigned int i = 0; i < numImages; i++) {
		m_SIFTImagesGPU[i].d_keyPoints = d_keyPoints + m_numKeyPointsPerImagePrefixSum[i];
		m_SIFTImagesGPU[i].d_keyPointDescs = d_keyPointDescs + m_numKeyPointsPerImagePrefixSum[i];
		//m_SIFTImagesGPU[i].d_keyPointCounter = d_keyPointCounters + i;
	}

	assert(getNumImages() == m_numKeyPointsPerImage.size());
	assert(getNumImages() == m_numKeyPointsPerImagePrefixSum.size());

	in.close();
}

void SIFTImageManager::alloc()
{
	m_numKeyPoints = 0;

	CUDA_SAFE_CALL(cudaMalloc(&d_keyPoints, sizeof(SIFTKeyPoint)*m_maxNumImages*m_maxKeyPointsPerImage));
	CUDA_SAFE_CALL(cudaMalloc(&d_keyPointDescs, sizeof(SIFTKeyPointDesc)*m_maxNumImages*m_maxKeyPointsPerImage));
	//CUDA_SAFE_CALL(cudaMalloc(&d_keyPointCounters, sizeof(int)*m_maxNumImages));
	//CUDA_SAFE_CALL(cudaMemset(d_keyPointCounters, 0, sizeof(int)*m_maxNumImages));

	// matching

	m_currImagePairMatches.resize(m_maxNumImages);

	const unsigned maxImageMatches = m_maxNumImages;
	CUDA_SAFE_CALL(cudaMalloc(&d_currNumMatchesPerImagePair, sizeof(int)*maxImageMatches));
	CUDA_SAFE_CALL(cudaMalloc(&d_currMatchDistances, sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW));
	CUDA_SAFE_CALL(cudaMalloc(&d_currMatchKeyPointIndices, sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW));

	CUDA_SAFE_CALL(cudaMalloc(&d_currNumFilteredMatchesPerImagePair, sizeof(int)*maxImageMatches));
	CUDA_SAFE_CALL(cudaMalloc(&d_currFilteredMatchDistances, sizeof(float)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED));
	CUDA_SAFE_CALL(cudaMalloc(&d_currFilteredMatchKeyPointIndices, sizeof(uint2)*maxImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED));
	CUDA_SAFE_CALL(cudaMalloc(&d_currFilteredTransforms, sizeof(float4x4)*maxImageMatches));
	CUDA_SAFE_CALL(cudaMalloc(&d_currFilteredTransformsInv, sizeof(float4x4)*maxImageMatches));

	m_validImages.resize(m_maxNumImages, 0);
	m_validImages[0] = 1; // first is valid
	CUDA_SAFE_CALL(cudaMalloc(&d_validImages, sizeof(int) *  m_maxNumImages));

	const unsigned int maxResiduals = MAX_MATCHES_PER_IMAGE_PAIR_FILTERED * (m_maxNumImages*(m_maxNumImages - 1)) / 2;
	m_globNumResiduals = 0;
	CUDA_SAFE_CALL(cudaMalloc(&d_globNumResiduals, sizeof(int)));
	CUDA_SAFE_CALL(cudaMemset(d_globNumResiduals, 0, sizeof(int)));
	CUDA_SAFE_CALL(cudaMalloc(&d_globMatches, sizeof(EntryJ)*maxResiduals));
	CUDA_SAFE_CALL(cudaMalloc(&d_globMatchesKeyPointIndices, sizeof(uint2)*maxResiduals));

	CUDA_SAFE_CALL(cudaMalloc(&d_fuseGlobalKeyCount, sizeof(int)));
	CUDA_SAFE_CALL(cudaMemset(d_fuseGlobalKeyCount, 0, sizeof(int)));
	CUDA_SAFE_CALL(cudaMalloc(&d_fuseGlobalKeyMarker, sizeof(int)*m_maxKeyPointsPerImage*m_submapSize));
	cutilSafeCall(cudaMemset(d_fuseGlobalKeyMarker, 0, sizeof(int)*m_maxKeyPointsPerImage*m_submapSize));

	initializeMatching();
}

void SIFTImageManager::free()
{
	m_SIFTImagesGPU.clear();

	m_numKeyPoints = 0;
	m_numKeyPointsPerImage.clear();
	m_numKeyPointsPerImagePrefixSum.clear();

	CUDA_SAFE_CALL(cudaFree(d_keyPoints));
	CUDA_SAFE_CALL(cudaFree(d_keyPointDescs));
	//CUDA_SAFE_CALL(cudaFree(d_keyPointCounters));

	m_currImagePairMatches.clear();

	CUDA_SAFE_CALL(cudaFree(d_currNumMatchesPerImagePair));
	CUDA_SAFE_CALL(cudaFree(d_currMatchDistances));
	CUDA_SAFE_CALL(cudaFree(d_currMatchKeyPointIndices));

	CUDA_SAFE_CALL(cudaFree(d_currNumFilteredMatchesPerImagePair));
	CUDA_SAFE_CALL(cudaFree(d_currFilteredMatchDistances));
	CUDA_SAFE_CALL(cudaFree(d_currFilteredMatchKeyPointIndices));
	CUDA_SAFE_CALL(cudaFree(d_currFilteredTransforms));
	CUDA_SAFE_CALL(cudaFree(d_currFilteredTransformsInv));

	m_validImages.clear();
	CUDA_SAFE_CALL(cudaFree(d_validImages));

	m_globNumResiduals = 0;
	CUDA_SAFE_CALL(cudaFree(d_globNumResiduals));
	CUDA_SAFE_CALL(cudaFree(d_globMatches));
	CUDA_SAFE_CALL(cudaFree(d_globMatchesKeyPointIndices));

	CUDA_SAFE_CALL(cudaFree(d_fuseGlobalKeyCount));
	CUDA_SAFE_CALL(cudaFree(d_fuseGlobalKeyMarker));
}

void SIFTImageManager::initializeMatching()
{
	for (unsigned int r = 0; r < m_maxNumImages; r++) {
		ImagePairMatch& imagePairMatch = m_currImagePairMatches[r];
		imagePairMatch.d_numMatches = d_currNumMatchesPerImagePair + r;
		imagePairMatch.d_distances = d_currMatchDistances + r * MAX_MATCHES_PER_IMAGE_PAIR_RAW;
		imagePairMatch.d_keyPointIndices = d_currMatchKeyPointIndices + r * MAX_MATCHES_PER_IMAGE_PAIR_RAW;
	}
}

void SIFTImageManager::fuseToGlobal(SIFTImageManager* global, const float4x4* transforms, unsigned int numTransforms) const
{
	//const unsigned int overlapImageIndex = getNumImages() - 1; // overlap frame

	std::vector<EntryJ> correspondences(m_globNumResiduals);
	cutilSafeCall(cudaMemcpy(correspondences.data(), d_globMatches, sizeof(EntryJ) * m_globNumResiduals, cudaMemcpyDeviceToHost));
	std::vector<uint2> correspondenceKeyIndices(m_globNumResiduals);
	cutilSafeCall(cudaMemcpy(correspondenceKeyIndices.data(), d_globMatchesKeyPointIndices, sizeof(uint2) * m_globNumResiduals, cudaMemcpyDeviceToHost));

	std::vector<SIFTKeyPoint> allKeys;
	getSIFTKeyPointsDEBUG(allKeys);
	std::vector<SIFTKeyPointDesc> allDesc;
	getSIFTKeyPointDescsDEBUG(allDesc);
	std::vector<bool> keyMarker(allKeys.size(), false);

	//!!!TODO camera stuff
	const float _colorIntrinsics[16] = {
		1180.31299f, 0.0f, 649.0f, 0.0f,
		0.0f, 1175.45569f, 483.5f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	const float4x4 colorIntrinsics(_colorIntrinsics);

	std::vector<SIFTKeyPoint> curKeys;
	std::vector<SIFTKeyPointDesc> curDesc;

	for (unsigned int i = 0; i < m_globNumResiduals; i++) {
		const EntryJ& corr = correspondences[i];
		if (corr.isValid()) {
			const uint2& keyIndices = correspondenceKeyIndices[i];
			uint2 k0 = make_uint2(corr.imgIdx_i, keyIndices.x);
			//uint2 k1 = make_uint2(corr.imgIdx_j, keyIndices.y);
			// pick first one (not overlap frame)

			if (!keyMarker[k0.y] && curKeys.size() < m_maxKeyPointsPerImage) {
				// project to first frame
				float3 pos = colorIntrinsics * (transforms[k0.x] * corr.pos_i);
				float2 loc = make_float2(pos.x / pos.z, pos.y / pos.z);
				SIFTKeyPoint key;
				key.pos = loc;
				key.scale = allKeys[k0.y].scale;
				key.depth = pos.z;
				curKeys.push_back(key);
				// desc
				curDesc.push_back(allDesc[k0.y]);
				keyMarker[k0.y] = true;
			} // not already found
		}// valid corr
	} // correspondences/residual

	unsigned int numKeys = (unsigned int)curKeys.size();
	SIFTImageGPU& cur = global->createSIFTImageGPU();
	cutilSafeCall(cudaMemcpy(cur.d_keyPoints, curKeys.data(), sizeof(SIFTKeyPoint) * numKeys, cudaMemcpyHostToDevice));
	cutilSafeCall(cudaMemcpy(cur.d_keyPointDescs, curDesc.data(), sizeof(SIFTKeyPointDesc) * numKeys, cudaMemcpyHostToDevice));
	global->finalizeSIFTImageGPU(numKeys);
}

void SIFTImageManager::filterFrames(unsigned int numCurrImagePairs)
{
	if (numCurrImagePairs == 0) return;
	
	int connected = 0;

	std::vector<unsigned int> currNumFilteredMatchesPerImagePair(numCurrImagePairs);
	cutilSafeCall(cudaMemcpy(currNumFilteredMatchesPerImagePair.data(), d_currNumFilteredMatchesPerImagePair, sizeof(unsigned int) * numCurrImagePairs, cudaMemcpyDeviceToHost));

	for (unsigned int i = 0; i < numCurrImagePairs; i++) { // previous frames
		if (currNumFilteredMatchesPerImagePair[i] > 0) {
			connected = 1;
			break;
		}
	}

	if (!connected)
		std::cout << "frame " << numCurrImagePairs << " not connected to previous!" << std::endl;

	m_validImages[numCurrImagePairs] = connected;
}

