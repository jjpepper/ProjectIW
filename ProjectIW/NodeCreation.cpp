#include <cassert>
#include "NodeCreation.h"
#include "WorldNode.h"
#include "MeshGeneration.h"
#include "NodeCache.h"

#define NUM_THREADS 1

NodeCreation::NodeCreation() : threadpool(NUM_THREADS)
{
	task_queue.reserve(16384);
	shutdown = false;
}

NodeCreation::~NodeCreation()
{
	threadpool.Quit();
}

void NodeCreation::Shutdown()
{
	shutdown = true;
}

unsigned int NodeCreation::TransferBatch(std::vector<DirtyNodeOutput>& batch)
{
	size_t count = batch.size();
	if (count == 0)
		return 0;

	using namespace std::chrono;
	steady_clock::time_point start = high_resolution_clock::now();

	//TODO: batch task creation for one single lock if necessary
	task_queue.clear();
	for (size_t i = 0; i < count; i++)
	{
		DirtyNodeOutput& node = batch[i];
		Task t([node, this]()
		{
			ProcessNode(node, &cache, shutdown);
		},
			nullptr, TaskPriority::Low);
		task_queue.push_back(t);

		//threadpool.AddTask(t);
	}
	threadpool.AddTaskBatch(task_queue);

	microseconds elapsed = duration_cast<microseconds>(high_resolution_clock::now() - start);
	return (unsigned int)elapsed.count();
}


void NodeCreation::ProcessNode(DirtyNodeOutput n, NodeCache* cache, std::atomic<bool>& shutdown)
{
	assert(n.node != 0);
	assert(n.node->GetStage() == NODE_STAGE_CREATION || n.node->GetStage() == NODE_STAGE_SPLITTABLE);

	NodeOutputType type = n.type;
	WorldNode* node = n.node;
	WorldNode* child = 0;

	switch (type)
	{
		//If the node needs division, divide it and assign the world handles.
	case NODE_OUTPUT_DIVIDE:
		node->Divide(cache);

		for (size_t i = 0; i < 8; i++)
		{
			PassNodeToMeshGeneration(node->children[i]);
		}
		node->SetStage(NODE_STAGE_READY);
		break;

	case NODE_OUTPUT_GROUP:
		for (size_t i = 0; i < 8; i++)
		{
			child = node->children[i];
			assert(child != 0);
			assert(!child->IsActive());
			child->parent = 0;
			child->is_leaf = true;
			cache->Add(child);
			node->children[i] = 0;
		}

		node->Group();
		node->SetStage(NODE_STAGE_READY);
		break;
	}

}

void NodeCreation::PassNodeToMeshGeneration(WorldNode* node)
{
	//assert(node->GetStage() == NODE_STAGE_CREATION);
	// First determine if the node needs its mesh generated by checking the stage of it.
	// The only way for it to not be NODE_STAGE_CREATION is if it was a cached node.
	if (node->GetStage() != NODE_STAGE_CREATION)
	{
		// Make sure the stage is one of these 3.
		// Side note: order of checks matter here since GetStage is called each check.
		assert(node->GetStage() == NODE_STAGE_MESH_GENERATING || node->GetStage() == NODE_STAGE_MESH_WAITING || node->GetStage() == NODE_STAGE_READY);
		return;
	}

	node->SetStage(NODE_STAGE_MESH_WAITING);
	MeshGeneration::AddNode(node);
}
