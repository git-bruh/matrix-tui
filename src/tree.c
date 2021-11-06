#include "stb_ds.h"
#include "termbox.h"
#include <assert.h>
#include <stdbool.h>

struct treeview_node *
treeview_node_alloc(struct treeview_node *parent, char *string, void *data) {
	struct treeview_node *node = malloc(sizeof(*node));

	if (node) {
		*node = (struct treeview_node){
			.parent = parent,
			.string = string,
			.data = data,
			.is_expanded = true,
		};
	}

	return node;
}

void
treeview_node_destroy(struct treeview_node *node) {
	if (!node) {
		return;
	}

	if (node->nodes) {
		for (size_t i = 0, len = arrlenu(tree->nodes); i < len; i++) {
			destroy(node->nodes[i]);
		}

		arrfree(node->nodes);
	}

	free(node);
}

/* If node is the parent's last child. */
static bool
is_last(const struct treeview_node *tree) {
	if (tree->parent) {
		size_t len = arrlenu(tree->parent->trees);

		if (len > 0 && tree == tree->parent->trees[len - 1]) {
			return true;
		}
	}

	return false;
}

static struct treeview_node *
leaf(struct treeview_node *tree) {
	if (tree->is_expanded && tree->trees) {
		size_t len = arrlenu(tree->trees);
		return leaf(tree->trees[len > 0 ? len - 1 : 0]);
	}

	return tree;
}

static struct treeview_node *
parent_next(struct treeview_node *tree) {
	if (tree->parent) {
		if ((tree->parent->index + 1) < arrlenu(tree->parent->trees)) {
			return tree->parent->trees[++tree->parent->index];
		}

		if (tree->parent->parent) {
			return parent_next(tree->parent);
		}
	}

	return tree;
}

/* Get the height of the tree upto the givcn node.
   the realheight variable will be set when we reach the node, otherwise
   it would retain it's original value.
   This is very hacky but it's the only way to do this recursively. */
static int
node_height(struct treeview_node *tree, struct treeview_node *node, int height,
			int *realheight) {
	if (!tree) {
		return height;
	}

	height++;

	if (!tree->is_expanded) {
		return height;
	}

	for (size_t i = 0, len = arrlenu(tree->trees); i < len; i++) {
		if (tree->trees[i] == node) {
			*realheight = height + 1;
			return height; /* Ignored. */
		}

		height = node_height(tree->trees[i], node, height, realheight);
	}

	return height;
}

static int
redraw(struct treeview *treeview, const struct treeview_node *tree, int x,
	   int y) {
	if (!tree) {
		return y;
	}

	/* Stolen from tview's semigraphics. */
	const char symbol[] = "├──";
	const char symbol_root[] = "───";
	const char symbol_end[] = "└──";
	const char symbol_continued[] = "│";
	const int gap_size = 3; /* Width of the above symbols (first 3). */

	bool is_end = is_last(tree);

	/* Skip the given offset before actually printing stuff. */
	if (treeview->skipped++ >= treeview->start_y) {
		/* Print the symbol and use the offset returned from that to print the
		 * data. */
		tb_print((x + (tb_print(x, y, TB_DEFAULT, TB_DEFAULT,
								tree->parent ? (is_end ? symbol_end : symbol)
											 : symbol_root))),
				 y, TB_DEFAULT,
				 tree == treeview->selected ? TB_REVERSE : TB_DEFAULT,
				 tree->data);

		y++; /* Next node will be on another line. */
	}

	if (!tree->is_expanded) {
		return y;
	}

	for (size_t i = 0, len = arrlenu(tree->trees); i < len; i++) {
		int delta = redraw(treeview, tree->trees[i], x + gap_size, y) - y;

		/* We can cheat here and avoid backtracking to show the parent-child
		 * relation by just filling the gaps as we would if we inspected them
		 * ourselves. */
		if (tree->parent && !is_end) {
			for (int j = 0; j < delta; j++) {
				tb_print(x, y++, TB_DEFAULT, TB_DEFAULT, symbol_continued);
			}
		} else {
			y += delta;
		}
	}

	return y;
}

void
treeview_redraw(struct treeview *treeview) {
	int rows = 0;
	int height = tb_height();

	node_height(treeview->root, treeview->selected, 0, &rows);

	if (rows == 0) {
		rows = 1;
	}

	int diff_forward = rows - (treeview->start_y + height);
	int diff_backward = treeview->start_y - (rows - 1);

	if (diff_backward > 0) {
		treeview->start_y -= diff_backward;
	} else if (diff_forward > 0) {
		treeview->start_y += diff_forward;
	}

	assert(treeview->start_y >= 0);
	assert(treeview->start_y < rows);

	redraw(treeview, treeview->root, 0, 0);
	/* Reset the number of skipped lines. */
	treeview->skipped = 0;
}

enum widget_error
treeview_event(struct treeview *treeview, enum treeview_event event) {
	switch (event) {
	case TREEVIEW_EXPAND:
		if (!tree->selected) {
			break;
		}

		tree->selected->is_expanded = !tree->selected->is_expanded;
		return WIDGET_REDRAW;
	case TB_KEY_ARROW_UP:
		if (!tree->selected) {
			break;
		}

		if (tree->selected->parent->index > 0) {
			tree->selected =
				tree->selected->parent->trees[--tree->selected->parent->index];
			tree->selected = leaf(tree->selected); /* Bottom node. */
		} else if (tree->selected->parent->parent) {
			tree->selected = tree->selected->parent;
		} else if (tree->selected == tree->root->trees[0]) {
			tree->start_y = 0; /* Scroll up to the title if we're already at the
								  top-most node. */
		} else {
			break;
		}

		return WIDGET_REDRAW;
	case TB_KEY_ARROW_DOWN:
		if (!tree->selected) {
			break;
		}

		if (tree->selected->is_expanded && tree->selected->trees) {
			tree->selected = tree->selected->trees[0]; /* First node. */
		} else {
			/* Ensure that we don't create a loop between the end-most node of
			 * the tree and it's parent at the root. */
			if (tree->selected != (leaf(tree->root))) {
				tree->selected = parent_next(tree->selected);
			}
		}

		return WIDGET_REDRAW;
	case TREEVIEW_INSERT: {
		if (!tree->selected) {
			break;
		}

		struct treeview_node *ntree = alloc(tree->selected);

		if (!ntree) {
			break;
		}

		arrput(tree->selected->trees, ntree);

		break;
	}
	case TREEVIEW_: {
		struct treeview_node *ntree =
			alloc(!tree->selected ? tree->root : tree->selected->parent);

		if (!ntree) {
			break;
		}

		arrput(ntree->parent->trees, ntree);

		/* We don't adjust indexes or set the selected tree unless it's the
		 * first entry. This is done to avoid accounting for the cases where we
		 * ascend to the top of a node, add a new node below it in the parent's
		 * trees and then
		 * try moving back to the node where all indices are set to 0. */
		if (!tree->selected) {
			tree->selected = ntree;
		}

		break;
	}
	case DELETE: {
		struct treeview_node *current = tree->selected;

		if (!current) {
			break;
		}

		arrdel(current->parent->trees, current->parent->index);

		if (current->parent->index < arrlenu(current->parent->trees)) {
			tree->selected = current->parent->trees[current->parent->index];
		} else if (current->parent->index > 0) {
			tree->selected = current->parent->trees[--current->parent->index];
		} else if (current->parent->parent) {
			/* Move up a level. */
			tree->selected = current->parent;
		} else {
			/* At top level and all nodes deleted. */
			tree->selected = NULL;
		}

		destroy(current);

		break;
	}
	}

	return WIDGET_NOOP;
}
