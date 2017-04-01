#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct PTNode
{
	char key;
	void* data_ptr;

	struct PTNode* right;
	struct PTNode* down;
};

struct PTTree
{
	struct PTNode* root;
};

/* Prefix Tree functions */
struct PTTree* pttree_create(void);
struct PTNode* pttree_search(struct PTTree* t, const char* key_ptr);
void pttree_insert(struct PTTree* t, const char* key_ptr, void* data_ptr);
void pttree_destroy(struct PTTree* t);

/* Prefix Tree Node functions */
struct PTNode* ptnode_create(const char key, void* data_ptr);
struct PTNode* ptnode_search(struct PTNode* n, const char* key_ptr);
void ptnode_insert(struct PTNode* n, const char* key_ptr, void* data_ptr);
void ptnode_destroy(struct PTNode* n);

#ifdef __cplusplus
}
#endif
