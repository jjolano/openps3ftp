#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct AVLNode
{
	int key;
	void* data_ptr;

	struct AVLNode* left;
	struct AVLNode* right;
};

struct AVLTree
{
	struct AVLNode* root;
};

/* AVL Tree functions */
struct AVLTree* avltree_create(void);
struct AVLNode* avltree_search(struct AVLTree* t, int key);
void avltree_insert(struct AVLTree* t, int key, void* data_ptr);
void avltree_balance(struct AVLTree* t);
void avltree_remove(struct AVLTree* t, int key);
void avltree_destroy(struct AVLTree* t);

/* AVL Node functions */
struct AVLNode* avlnode_create(int key, void* data_ptr);
struct AVLNode* avlnode_search(struct AVLNode* n, int key);
struct AVLNode* avlnode_ip(struct AVLNode* n);
struct AVLNode* avlnode_removenode(struct AVLNode* n);
struct AVLNode* avlnode_remove(struct AVLNode* n, int key);
int avlnode_height(struct AVLNode* n);
int avlnode_balance_factor(struct AVLNode* n);
struct AVLNode* avlnode_rotate_ll(struct AVLNode* n);
struct AVLNode* avlnode_rotate_lr(struct AVLNode* n);
struct AVLNode* avlnode_rotate_rl(struct AVLNode* n);
struct AVLNode* avlnode_rotate_rr(struct AVLNode* n);
struct AVLNode* avlnode_balance(struct AVLNode* n);
void avlnode_destroy(struct AVLNode* n);

#ifdef __cplusplus
}
#endif
