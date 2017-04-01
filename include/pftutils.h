#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct PTNode
{
	void* ptr;
	struct PTNode* children[26];
};

/* Prefix Tree functions */
struct PTNode* ptnode_init(void);
void ptnode_insert(struct PTNode* root, const char* key, void* ptr);
struct PTNode* ptnode_nodesearch(struct PTNode* root, const char* key);
void* ptnode_search(struct PTNode* root, const char* key);
void ptnode_free(struct PTNode* root);

#ifdef __cplusplus
}
#endif
