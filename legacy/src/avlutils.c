#include "common.h"
#include "avlutils.h"

struct AVLTree* avltree_create(void)
{
	struct AVLTree* t = malloc(sizeof(struct AVLTree));

	if(t)
	{
		t->root = NULL;
	}

	return t;
}

struct AVLNode* avltree_search(struct AVLTree* t, int key)
{
	return avlnode_search(t->root, key);
}

void avltree_insert(struct AVLTree* t, int key, void* data_ptr)
{
	struct AVLNode* next = t->root;
	struct AVLNode* last = NULL;

	// find a suitable location
	while(next != NULL)
	{
		last = next;

		if(key == next->key)
		{
			// already exists
			break;
		}

		next = key < next->key ? next->left : next->right;
	}

	if(next == NULL)
	{
		// insert node
		struct AVLNode* n = avlnode_create(key, data_ptr);

		if(last != NULL)
		{
			if(key < last->key)
			{
				last->left = n;
			}
			else
			{
				last->right = n;
			}

			avltree_balance(t);
		}
		else
		{
			t->root = n;
		}
	}
}

void avltree_balance(struct AVLTree* t)
{
	struct AVLNode* root = avlnode_balance(t->root);

	if(root != t->root)
	{
		t->root = root;
	}
}

void avltree_remove(struct AVLTree* t, int key)
{
	t->root = avlnode_remove(t->root, key);
}

void avltree_destroy(struct AVLTree* t)
{
	avlnode_destroy(t->root);
	free(t);
}

struct AVLNode* avlnode_create(int key, void* data_ptr)
{
	struct AVLNode* n = malloc(sizeof(struct AVLNode));

	if(n)
	{
		n->key = key;
		n->data_ptr = data_ptr;

		n->left = NULL;
		n->right = NULL;
	}

	return n;
}

struct AVLNode* avlnode_search(struct AVLNode* n, int key)
{
	struct AVLNode* current = n;

	while(current != NULL && current->key != key)
	{
		current = key < current->key ? current->left : current->right;
	}

	return current;
}

struct AVLNode* avlnode_ip(struct AVLNode* n)
{
	struct AVLNode* current = n->left;

	while(current->right != NULL)
	{
		current = current->right;
	}

	return current;
}

struct AVLNode* avlnode_removenode(struct AVLNode* n)
{
	if(n->left != NULL && n->right != NULL)
	{
		// swap
		struct AVLNode* ip = avlnode_ip(n);

		n->key = ip->key;
		n->data_ptr = ip->data_ptr;

		n->left = avlnode_remove(n->left, ip->key);
		return n;
	}

	struct AVLNode* successor = n->left != NULL ? n->left : n->right;
	free(n);
	return successor;
}

struct AVLNode* avlnode_remove(struct AVLNode* n, int key)
{
	if(n == NULL)
	{
		return NULL;
	}

	if(key == n->key)
	{
		n = avlnode_removenode(n);
	}
	else
	{
		if(key < n->key)
		{
			n->left = avlnode_remove(n->left, key);
		}
		else
		{
			n->right = avlnode_remove(n->right, key);
		}
	}

	return n != NULL ? avlnode_balance(n) : NULL;
}

int avlnode_height(struct AVLNode* n)
{
	int height_left = 0;
	int height_right = 0;

	if(n->left)
	{
		height_left = avlnode_height(n->left);
	}

	if(n->right)
	{
		height_right = avlnode_height(n->right);
	}

	return height_right > height_left ? ++height_right : ++height_left;
}

int avlnode_balance_factor(struct AVLNode* n)
{
	int bf = 0;

	if(n->left)
	{
		bf -= avlnode_height(n->left);
	}

	if(n->right)
	{
		bf += avlnode_height(n->right);
	}

	return bf;
}

struct AVLNode* avlnode_rotate_ll(struct AVLNode* n)
{
	struct AVLNode* a = n;
	struct AVLNode* b = a->left;

	a->left = b->right;
	b->right = a;

	return b;
}

struct AVLNode* avlnode_rotate_lr(struct AVLNode* n)
{
	struct AVLNode* a = n;
	struct AVLNode* b = a->left;
	struct AVLNode* c = b->right;

	a->left = c->right;
	b->right = c->left;
	c->left = b;
	c->right = a;

	return c;
}

struct AVLNode* avlnode_rotate_rl(struct AVLNode* n)
{
	struct AVLNode* a = n;
	struct AVLNode* b = a->right;
	struct AVLNode* c = b->left;

	a->right = c->left;
	b->left = c->right;
	c->right = b;
	c->left = a;

	return c;
}

struct AVLNode* avlnode_rotate_rr(struct AVLNode* n)
{
	struct AVLNode* a = n;
	struct AVLNode* b = a->right;

	a->right = b->left;
	b->left = a;

	return b;
}

struct AVLNode* avlnode_balance(struct AVLNode* n)
{
	struct AVLNode* root = n;

	if(n->left)
	{
		n->left = avlnode_balance(n->left);
	}

	if(n->right)
	{
		n->right = avlnode_balance(n->right);
	}

	int bf = avlnode_balance_factor(n);

	if(bf >= 2)
	{
		// right
		root = avlnode_balance_factor(n->right) < 0 ? avlnode_rotate_rl(n) : avlnode_rotate_rr(n);
	}

	if(bf <= -2)
	{
		// left
		root = avlnode_balance_factor(n->left) > 0 ? avlnode_rotate_lr(n) : avlnode_rotate_ll(n);
	}

	return root;
}

void avlnode_destroy(struct AVLNode* n)
{
	if(n)
	{
		avlnode_destroy(n->left);
		avlnode_destroy(n->right);
		free(n);
	}
}
