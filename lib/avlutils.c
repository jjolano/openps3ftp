#include "avlutils.h"

/* fancy-tree.c and simple-tree.c by Michael Burrell */

struct ClientNode** search_subtree(struct ClientNode** n, int x) {
	if (*n == NULL) {	// does not exist in the tree
		return n;
	} else if (x == (*n)->data) {	// have found it in the tree
		return n;
	} else if (x > (*n)->data) {
		return search_subtree(&(*n)->right, x);
	} else {
		return search_subtree(&(*n)->left, x);
	}
}

bool exists_in_subtree(struct ClientNode const* n, int x) {
	if (n == NULL) {
		return false;
	} else if (x == n->data) {
		return true;
	} else if (x > n->data) {
		return exists_in_subtree(n->right, x);
	} else {
		return exists_in_subtree(n->left, x);
	}
}

bool exists_in_tree(struct ServerClients const* t, int x) {
	return exists_in_subtree(t->root, x);
}

int height(struct ClientNode const* n) {
	int left = n->left == NULL ? 0 : n->left->height;
	int right = n->right == NULL ? 0 : n->right->height;
	return 1 + (left > right ? left : right);	// 1 + max(left, right)
}

int balance_factor(struct ClientNode const* n) {
	int left = n->left == NULL ? 0 : n->left->height;
	int right = n->right == NULL ? 0 : n->right->height;
	return right - left;
}

struct ClientNode* rebalance(struct ClientNode* n) {
	if (balance_factor(n) == -2 && balance_factor(n->left) == +1) {	// LR
		struct ClientNode* const X = n,
			* const Y = X->left,
			* const D = X->right,
			* const A = Y->left,
			* const Z = Y->right,
			* const B = Z->left,
			* const C = Z->right;
		X->left = Z;
		X->right = D;
		Z->left = Y;
		Z->right = C;
		Y->left = A;
		Y->right = B;
		n = X;	// root stays as X
		Y->height = height(Y);
		Z->height = height(Z);
		X->height = height(X);
	}
	if (balance_factor(n) == -2 && balance_factor(n->left) <= 0) {	// LL
		struct ClientNode* const X = n,
			* const Y = X->left,
			* const D = X->right,
			* const Z = Y->left,
			* const C = Y->right,
			* const A = Z->left,
			* const B = Z->right;
		Y->left = Z;
		Y->right = X;
		Z->left = A;
		Z->right = B;
		X->left = C;
		X->right = D;
		n = Y;	// the new root of this subtree is Y
		Z->height = height(Z);
		X->height = height(X);
		Y->height = height(Y);
	}

	if (balance_factor(n) == +2 && balance_factor(n->right) == -1) {	// RL
		struct ClientNode* const X = n,
			* const Y = X->right,
			* const D = X->left,
			* const A = Y->right,
			* const Z = Y->left,
			* const B = Z->right,
			* const C = Z->left;
		X->right = Z;
		X->left = D;
		Z->right = Y;
		Z->left = C;
		Y->right = A;
		Y->left = B;
		n = X;	// root stays as X
		Y->height = height(Y);
		Z->height = height(Z);
		X->height = height(X);
	}
	if (balance_factor(n) == +2 && balance_factor(n->right) >= 0) {	// RR
		struct ClientNode* const X = n,
			* const Y = X->right,
			* const D = X->left,
			* const Z = Y->right,
			* const C = Y->left,
			* const A = Z->right,
			* const B = Z->left;
		Y->right = Z;
		Y->left = X;
		Z->right = A;
		Z->left = B;
		X->right = C;
		X->left = D;
		n = Y;	// the new root of this subtree is Y
		Z->height = height(Z);
		X->height = height(X);
		Y->height = height(Y);
	}

	return n;
}

struct ClientNode* insert_in_subtree(struct ClientNode* n, int x, struct Client* client) {
	if (n == NULL) {
		n = malloc(sizeof *n);
		n->data = x;
		n->client = client;
		n->left = NULL;
		n->right = NULL;
	} else if (x == n->data) {	// it already exists
		// do nothing
	} else if (x > n->data) {
		n->right = insert_in_subtree(n->right, x, client);
	} else {
		n->left = insert_in_subtree(n->left, x, client);
	}
	n->height = height(n);
	return rebalance(n);
}

void insert_node(struct ServerClients* t, int x, struct Client* client) {
	t->root = insert_in_subtree(t->root, x, client);
}

struct ClientNode* immediate_predecessor(struct ClientNode* n) {
	n = n->left;
	while (n->right != NULL) {
		n = n->right;
	}
	return n;
}

struct ClientNode* remove_node(struct ClientNode* n) {
	if (n->left == NULL && n->right == NULL) {
		free(n);
		return NULL;
	} else if (n->left == NULL && n->right != NULL) {
		struct ClientNode* only_child = n->right;
		free(n);
		return only_child;
	} else if (n->left != NULL && n->right == NULL) {
		struct ClientNode* only_child = n->left;
		free(n);
		return only_child;		
	} else {
		struct ClientNode* immed_pred = immediate_predecessor(n);

		n->data = immed_pred->data;
		n->client = immed_pred->client;

		n->left = remove_from_subtree(n->left, immed_pred->data);
		return n;
	}
}

struct ClientNode* remove_from_subtree(struct ClientNode* n, int x) {
	if (n == NULL) {
		// do nothing
	} else if (x == n->data) {
		n = remove_node(n);
	} else if (x > n->data) {
		n->right = remove_from_subtree(n->right, x);
	} else {
		n->left = remove_from_subtree(n->left, x);
	}

	if(n == NULL)
	{
		return NULL;
	}

	n->height = height(n);
	return rebalance(n);
}

void remove_from_tree(struct ServerClients* t, int x) {
	t->root = remove_from_subtree(t->root, x);
}

void destroy_subtree(struct ClientNode* n) {
	if (n == NULL) {
		// do nothing
	} else {
		destroy_subtree(n->left);
		destroy_subtree(n->right);
		free(n);
	}
}

void destroy_tree(struct ServerClients* t) {
	destroy_subtree(t->root);
}
