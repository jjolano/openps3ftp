#include "common.h"
#include "pftutils.h"

struct PTTree* pttree_create(void)
{
	struct PTTree* t = malloc(sizeof(struct PTTree));

	if(t)
	{
		t->root = NULL;
	}

	return t;
}

struct PTNode* pttree_search(struct PTTree* t, const char* key_ptr)
{
	return ptnode_search(t->root, key_ptr);
}

void pttree_insert(struct PTTree* t, const char* key_ptr, void* data_ptr)
{
	if(t->root == NULL)
	{
		t->root = ptnode_create(*key_ptr, NULL);	
	}
	
	ptnode_insert(t->root, key_ptr, data_ptr);
}

void pttree_destroy(struct PTTree* t)
{
	ptnode_destroy(t->root);
	free(t);
}

struct PTNode* ptnode_create(const char key, void* data_ptr)
{
	struct PTNode* n = malloc(sizeof(struct PTNode));

	if(n)
	{
		n->key = key;
		n->data_ptr = data_ptr;

		n->right = NULL;
		n->down = NULL;
	}

	return n;
}

struct PTNode* ptnode_search(struct PTNode* n, const char* key_ptr)
{
	if(n == NULL)
	{
		return NULL;
	}

	if(n->key != *key_ptr)
	{
		// right
		return ptnode_search(n->right, key_ptr);
	}

	if(*key_ptr == '\0')
	{
		return n;
	}
	
	// down
	return ptnode_search(n->down, key_ptr + 1);
}

void ptnode_insert(struct PTNode* n, const char* key_ptr, void* data_ptr)
{
	if(n->key == '\0')
	{
		// insert
		n->data_ptr = data_ptr;
		return;
	}

	if(n->key != *key_ptr)
	{
		// move right
		if(n->right == NULL)
		{
			n->right = ptnode_create(*key_ptr, NULL);
		}

		ptnode_insert(n->right, key_ptr, data_ptr);
	}
	else
	{
		// move down
		if(n->down == NULL)
		{
			n->down = ptnode_create(key_ptr[1], NULL);
		}

		ptnode_insert(n->down, key_ptr + 1, data_ptr);
	}
}

void ptnode_destroy(struct PTNode* n)
{
	if(n->right)
	{
		ptnode_destroy(n->right);
	}

	if(n->down)
	{
		ptnode_destroy(n->down);
	}

	free(n);
}
