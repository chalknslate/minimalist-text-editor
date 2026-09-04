#include <stdio.h>
#include <stdlib.h>

struct pool_node {
	struct pool_node* next;
};

struct pool {
	void* memory;
	struct pool_node* fl;
	size_t sz;
	size_t cap;
};

int pool_create(struct pool* p, size_t size, size_t objectcount) {
	p->memory = malloc(size*objectcount);
	if(p->memory == NULL) {
		printf("Failed to allocate memory.");
		exit(1);
	}
	p->sz = size;
	p->cap = objectcount;
	p->fl = NULL;
	for(size_t i = 0; i< objectcount; i++) {
		struct pool_node * n = (struct pool_node*)(char*)(p->memory+(i*size));
		n->next = p->fl;
		p->fl = n;
	}
}

void* alloc_pool(struct pool* p) {
	if(p->fl == NULL) {
		return NULL;
	}
	struct pool_node* n = p->fl;
	p->fl = n->next; 
	return n;	
}
void free_pool(struct pool* p, void* ptr) {
	struct pool_node* n = ptr;
	n->next = p->fl;
	p->fl = n;
}
int destroy_pool(struct pool* p) {
	free(p->memory);
	return 0;
}
int main(int argc, char*argv[]) {
	struct pool p;
	pool_create(&p, sizeof(int), 64);
	int * a = alloc_pool(&p);
	*a = 42;
	printf("%d\n", *a);
	destroy_pool(&p);
	return 0;
}