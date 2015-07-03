/* $Id: treap.h 2950 2006-08-25 12:30:18Z wheirman $ */

/*
rd_measure
Copyright (C) 2003  Kristof Beyls

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef TREAP_H
#define TREAP_H

using namespace std;

#define NDEBUG

#include <stdlib.h>
#include <assert.h>
#include <string>
#include <stdio.h>
/*#include <vector>*/
/*#include <hash_map> // hash_map is buggy */
/*#include <functional>*/

#define DEBUG_TREAP 0

/*#define HASH_TABLE_SIZE 1024*1024*/

/*#if DEBUG_TREAP >=2*/
#include <iostream>
#include <fstream>
/*#include "daVinci-interface.h"*/
#include <utility>
/*#endif*/

/* hash_map: see section 17.6 in 'The C++ programming language, vol. 3'
   by B. Stroustrup */
/*
class hash_map {
private:
  struct Entry {
    const void* const key;
    treap* val;
    Entry* next;
    Entry(const void* const k, treap* v, Entry* n)
	: key(k), val(v), next(n) {}
  };

  vector<Entry> v; // the actual entries
  vector<Entry*> b; // the hash table: pointers into v

  static const float max_load=0.7;
  static const float grow=1.6;
  static const size_t init_size=256;
public:
  hash_map() {
    v.reserve(init_size);
  }
};
*/

class treap;

class hash_map_voidp_treap {
private:
  struct bucket {
    const void*const key;
    treap* value;
    bucket* next;
    bucket(const void*const k, treap* v) : key(k), value(v), next(0) {}
  };
  bucket** table;
  unsigned long table_size;
  unsigned long table_size_log2;
  unsigned long n_items;

  unsigned long index_mask;

  //  static const float grow_percentage = 1.7;
#define max_fill_percentage  0.7
  //  static const float max_fill_percentage = 0.7;

  inline unsigned long index(const void*const k) const {
    //return (((unsigned long)k)>>2)%table_size;
    return (((unsigned long)k)>>2) & index_mask;
  }
  
  inline unsigned long index(const void*const k, unsigned long index_mask) const {
    //return (((unsigned long)k)>>2)%tab_size;
    return (((unsigned long)k)>>2) & index_mask;
  }
  

  void resize() {
    unsigned long old_table_size = table_size;
    unsigned long new_table_size = table_size<<1;// *grow_percentage;
    unsigned long new_table_size_log2 = table_size_log2+1;
    unsigned long new_index_mask = 0;
    for(unsigned i=0;i<new_table_size_log2; i++)
    {
       new_index_mask <<= 1; 
       new_index_mask++;
    }

    bucket** new_table = new bucket*[new_table_size];
    for(unsigned i=0;i<new_table_size; i++)
        new_table[i]=0;
    for(unsigned long i=0;i<old_table_size; i++) {
       for(bucket *b=table[i]; b!=0;)
       {
            const void* const key = b->key;
	    // insert in new table
	    if (new_table[index(key, new_index_mask)]==0)
	    {
	       new_table[index(key, new_index_mask)]=b;
               b=b->next;
               new_table[index(key, new_index_mask)]->next=0;
	    }
            else {
	       bucket* last_bucket=new_table[index(key, new_index_mask)];
	       assert(last_bucket!=0);
	       while (last_bucket->next!=0) last_bucket=last_bucket->next;
	       last_bucket->next=b;
               b=b->next;
               last_bucket->next->next=0;
            }
       }
    }
    delete[] table;
    table = new_table;
    table_size = new_table_size;
    table_size_log2 = new_table_size_log2;
    index_mask = new_index_mask;
  }

public:
  hash_map_voidp_treap(unsigned long log2_size=7) 
  : table(new bucket*[1<<log2_size]), table_size(1<<log2_size), table_size_log2(log2_size), n_items(0)
  {
    index_mask = 0;
    for(unsigned i=0;i<table_size_log2; i++)
    {
       index_mask <<= 1; 
       index_mask++;
    }
    for(unsigned i=0;i<table_size;i++)
       table[i]=0;
  }
  ~hash_map_voidp_treap() 
  {
    delete[] table;
  }
  treap* find(const void* const key) const {
    if (table[index(key)]==0)
       return 0;
    for(bucket* i=table[index(key)]; i!=0; i=i->next) {
       if (i->key==key)
         return i->value;
    }
    return 0;
  }
  void add(const void* const key, treap* value) {
    if (n_items > max_fill_percentage*table_size)
      resize();
    n_items++;
    if (table[index(key)]==0)
    {
       table[index(key)]=new bucket(key, value);
       return;
    }
    bucket* last_bucket=table[index(key)];
    assert(last_bucket!=0);
    while (last_bucket->next!=0) last_bucket=last_bucket->next;
    last_bucket->next = new bucket(key,value);
    return;
  }
};

class rd {
public:
  hash_map_voidp_treap hash_table/*[HASH_TABLE_SIZE]*/;
  treap * first_treap;
  treap * left_most_treap;

  rd(void) : first_treap(NULL), left_most_treap(NULL) {}
  inline treap* lookup_addr(const void* const addr);
  pair<unsigned long, long long> reference(const void* const addr, long long current_array_ref, unsigned int *seedp);
  unsigned long distance(const void* const addr);
};

class treap {
  friend class rd;
private:/*
  struct addr_cmp : public binary_function<const treap* const, const treap* const, bool> {
  public:
    inline bool operator()(const treap* const & left, const treap* const & right) const {
      return left->address<right->address;
    }
  };
  friend class treap::addr_cmp;
*/
  unsigned int seedp; // for rand_r
  inline void check_tree() {
#ifndef NDEBUG
    // if it is known that the tree is not consisten, return
    if (!tree_consistent)
      return;
    treap* root = find_root();
    assert(root->parent==0);
    assert(root->rank == root->calculate_rank());
    if (!(root->parent==0 && root->left==0 && root->right==0))
      assert(root->is_on_left_path());
    if (root->left!=0) root->left->check_node();
    if (root->right!=0) root->right->check_node();
#endif
  }

  struct hash_function {
    inline size_t operator()(const void* const addr) const { return (size_t) addr; }
  };

/*
  static inline unsigned hash_bucket(const void* const addr) {
    // only correct for 32-bit architecture
    return (((unsigned long)addr)<<2)%HASH_TABLE_SIZE;
  }*/

  inline void add_to_hash_table() {
    //hash_table/*[i]*/[this->address] = this;
    rdparent->hash_table.add(this->address, this);
/*
    treap* t=hash_table[i];
    if (t==0)
      hash_table[i]=this;
    else {
      while (t->next_in_hash!=0)
	t=t->next_in_hash;
      t->next_in_hash=this;
    }
*/
  }


private:
  treap *left, *right, *parent;
  const unsigned int priority;
  /*  unsigned int node_nr;*/
  const void * const address;
  long long last_array_ref;
  //treap *next_in_hash;
  
  /* 64 bits. highest bit says if it this is node can be reached by
     only going left from the root. 63 other bits give the rank number
     of this node. */
  /*  unsigned long long info;*/
  // rank is the number of nodes in the left subtree
  unsigned long rank;
  bool on_left_flag;

  string node_name() const {
    char name[32];
    sprintf(name,"%p",address);
    return string(name);
  }
private:
  static const unsigned long long ON_LEFT_FLAG = 0x8000000000000000ULL;
  static const unsigned long long CLEAR_LEFT_FLAG = 0x7FFFFFFFFFFFFFFFULL;
  static const unsigned long long RANK_BITS = CLEAR_LEFT_FLAG;
  static const unsigned long long NOT_RANK_BITS = ON_LEFT_FLAG;

  // for debugging in check_tree:
#ifndef NDEBUG
  static bool tree_consistent;
#endif

  inline bool is_on_left_path() const { return on_left_flag; }
  inline void set_on_left_path() { on_left_flag=true; }
  inline void clear_on_left_path() { on_left_flag=false; }
  /*  inline bool is_on_left_path() const { return info & ON_LEFT_FLAG; }
  inline void set_on_left_path() { info |= ON_LEFT_FLAG; }
  inline void clear_on_left_path() { info &= CLEAR_LEFT_FLAG; }*/

  /* rank is really the number of nodes in the left subtree. */
  /*  inline unsigned long long get_rank() const { return info & RANK_BITS; }
  inline void set_rank(unsigned long long rank) {
    info = (info & NOT_RANK_BITS) | (rank & RANK_BITS);
    } */
  inline unsigned long get_rank() const { return rank; }
  inline void set_rank(unsigned long _rank) {
    rank = _rank;
  }
  inline void decrease_rank() {
    rank--;
  }
  inline void increase_rank() {
    rank++;
  }


protected:
  /* see p.59 of 'Design and Analysis of Algorithms' by Dexter
     C. Kozen. The this node is rotated up. */
  inline void rotate() {
    check_tree();
#if DEBUG_TREAP >0
    cout<<"treap::rotate("<<node_name()<<","<<parent->node_name()<<")"<<endl;
#endif
    assert(parent!=0);
    treap* const y=parent;
#if DEBUG_TREAP >1
    {
      if (parent!=0)
      daVinci_connection.delete_edge((node_name()+"_"+parent->node_name()+"p").c_str());
    }
#endif
    parent=y->parent;
#if DEBUG_TREAP >1
    {
      if (parent!=0)
      daVinci_connection.add_edge((node_name()+"_"+parent->node_name()+"p").c_str(),
        "parent_edge", "[]", node_name().c_str(), parent->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
#if DEBUG_TREAP >1
    {
      if (y->parent!=0)
      daVinci_connection.delete_edge((y->node_name()+"_"+y->parent->node_name()+"p").c_str());
    }
#endif
    y->parent=this;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((y->node_name()+"_"+node_name()+"p").c_str(),
        "parent_edge", "[]", y->node_name().c_str(), node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
    if (parent!=0) {
      if (parent->left==y) {
#if DEBUG_TREAP >1
    {
      if (parent->left!=0)
      daVinci_connection.delete_edge((parent->node_name()+"_"+parent->left->node_name()+"l").c_str());
    }
#endif
	parent->left=this;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((parent->node_name()+"_"+this->node_name()+"l").c_str(),
        "left_edge", "[]", parent->node_name().c_str(), this->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      } else {
	assert(parent->right==y);
#if DEBUG_TREAP >1
    {
      if (parent->right!=0)
      daVinci_connection.delete_edge((parent->node_name()+"_"+parent->right->node_name()+"r").c_str());
    }
#endif
	parent->right=this;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((parent->node_name()+"_"+this->node_name()+"r").c_str(),
        "right_edge", "[]", parent->node_name().c_str(), this->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      }
    }
    if (y->left==this) {
#if DEBUG_TREAP >1
    {
      daVinci_connection.delete_edge((y->node_name()+"_"+y->left->node_name()+"l").c_str());
    }
#endif
       y->left = right;
#if DEBUG_TREAP >1
    {
      if (right!=0)
      daVinci_connection.add_edge((y->node_name()+"_"+right->node_name()+"l").c_str(),
        "left_edge", "[]", y->node_name().c_str(), right->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      if (right!=0) {
#if DEBUG_TREAP >1
    {
      daVinci_connection.delete_edge((right->node_name()+"_"+right->parent->node_name()+"p").c_str());
    }
#endif
        right->parent = y;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((right->node_name()+"_"+y->node_name()+"p").c_str(),
        "parent_edge", "[]", right->node_name().c_str(), y->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      }
#if DEBUG_TREAP >1
    {
      if (right!=0)
      daVinci_connection.delete_edge((node_name()+"_"+right->node_name()+"r").c_str());
    }
#endif
      right = y;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((this->node_name()+"_"+y->node_name()+"r").c_str(),
        "right_edge", "[]", this->node_name().c_str(), y->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      y->clear_on_left_path();
      y->set_rank(y->get_rank() - get_rank() - 1);
#if DEBUG_TREAP >1
    {
      daVinci_connection.change_node_attributes(y->node_name().c_str(),y->daVinci_node_attributes().c_str());
    }
#endif
    } else {
      assert(y->right==this);
#if DEBUG_TREAP >1
    {
      daVinci_connection.delete_edge((y->node_name()+"_"+y->right->node_name()+"r").c_str());
    }
#endif
      y->right = left;
#if DEBUG_TREAP >1
    {
      if (left!=0)
      daVinci_connection.add_edge((y->node_name()+"_"+left->node_name()+"r").c_str(),
        "right_edge", "[]", y->node_name().c_str(), left->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      if (left!=0)  {
#if DEBUG_TREAP >1
    {
      daVinci_connection.delete_edge((left->node_name()+"_"+left->parent->node_name()+"p").c_str());
    }
#endif
        left->parent = y;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((left->node_name()+"_"+y->node_name()+"p").c_str(),
        "parent_edge", "[]", left->node_name().c_str(), y->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      }
#if DEBUG_TREAP >1
    {
      if (left!=0)
      daVinci_connection.delete_edge((node_name()+"_"+left->node_name()+"l").c_str());
    }
#endif
      left = y;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((this->node_name()+"_"+y->node_name()+"l").c_str(),
        "left_edge", "[]", this->node_name().c_str(), y->node_name().c_str());
      //daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      set_rank(get_rank() + y->get_rank() + 1);
      if (y->is_on_left_path())
	set_on_left_path();
#if DEBUG_TREAP >1
    {
      daVinci_connection.change_node_attributes(node_name().c_str(),daVinci_node_attributes().c_str());
    }
#endif
    }
    check_tree();

  }
  /** rotate_up rotates a node up until it is at a position in the
      treap where its parent has higher priority and its children have
      lower priority. */
  inline void rotate_up() {
#if DEBUG_TREAP >0
    cout<<"treap::rotate_up("<<node_name()<<")"<<endl;
#endif
    //check_tree();
    while (parent!=0 && parent->priority < priority) rotate();
    //check_tree();
  }


  inline void rotate_down() {
    check_tree();
#if DEBUG_TREAP >0
    cout<<"treap::rotate_down("<<node_name()<<")"<<endl;
#endif
    /*
    treap* old_p = this;
    for(treap* p=parent;p!=0; old_p=p, p=p->parent)
      {
	if (old_p == p->left) {
	  //p->set_rank(p->get_rank()-1);
	  p->decrease_rank();
	}
      }
    */

    while (left!=0 || right!=0) {
      if (left==0) {
	//	right->rank++;
	right->rotate();
	//parent->decrease_rank();
      }
      else if (right==0) {
	left->rotate();
      } else if (left->priority > right->priority) {
	left->rotate();
      } else {
	//	right->rank++;
	right->rotate();
	//parent->decrease_rank();
      }
    }
    check_tree();
  }

private:
  /*inline*/ void decrease_rank_for_removal() {
    treap* old_p = this;
    for(treap* p=parent;p!=0; old_p=p, p=p->parent)
      {
	if (old_p == p->left) {
	  //p->set_rank(p->get_rank()-1);
	  p->decrease_rank();
#if DEBUG_TREAP >1
	  //	  daVinci_connection.wait_for_menu_selection("next");
	  daVinci_connection.status("REMOVE: UPDATING RANK OF PARENTS");
	  daVinci_connection.change_node_attributes(p->node_name().c_str(),p->daVinci_node_attributes().c_str());
	  //	  daVinci_connection.wait_for_menu_selection("next");
#endif
	}
      }
  }

public: 
  /*inline*/ void remove() {
#if DEBUG_TREAP >0
    cout<<"treap::remove("<<node_name()<<")"<<endl;
#endif
    check_tree();
    rotate_down();
    // lower rank of all parents by 1
    /* KB 23.04.2001 trying to lower the rank of the parents 
       in rotate_down, to optimize the speed of the simulation
       SHOULD TRY TO ELIMINATE THE DECREASING OF THE RANK UNTIL
       THE PARENT.
    */
    decrease_rank_for_removal();
    /*
    treap* old_p = this;
    for(treap* p=parent;p!=0; old_p=p, p=p->parent)
      {
	if (old_p == p->left) {
	  //p->set_rank(p->get_rank()-1);
	  p->decrease_rank();
#if DEBUG_TREAP >1
	  //	  daVinci_connection.wait_for_menu_selection("next");
	  daVinci_connection.status("REMOVE: UPDATING RANK OF PARENTS");
	  daVinci_connection.change_node_attributes(p->node_name().c_str(),p->daVinci_node_attributes().c_str());
	  //	  daVinci_connection.wait_for_menu_selection("next");
#endif
	}
      }
    */
    if (parent!=0)
      {
	if (parent->left==this)
        {
#if DEBUG_TREAP >1
	  {
	    if (parent) daVinci_connection.delete_edge((parent->node_name()+"_"+node_name()+"l").c_str());
	    //      daVinci_connection.wait_for_menu_selection("next");
	  }
#endif
	  parent->left=0; 
        }
	else {
	  assert(parent->right==this);
#if DEBUG_TREAP >1
    {
      if (parent) daVinci_connection.delete_edge((parent->node_name()+"_"+node_name()+"r").c_str());
      //      daVinci_connection.wait_for_menu_selection("next");
    }
#endif
	  parent->right=0;
	}
      }
#if DEBUG_TREAP >1
    {
      if (left) daVinci_connection.delete_edge((node_name()+"_"+left->node_name()+"l").c_str());
      if (right) daVinci_connection.delete_edge((node_name()+"_"+right->node_name()+"r").c_str());
      if (parent) daVinci_connection.delete_edge((node_name()+"_"+parent->node_name()+"p").c_str());
      daVinci_connection.delete_node(node_name().c_str());
    }
#endif
    parent=left=right=0;
    check_tree();
  }

public:
  /** assuming that left_most is the node with the lowest in-order
      ordering, add this node with lower in-order ordering */
  /*inline*/ void insert_left(treap* left_most) {
    if (left_most != 0 ) check_tree();
    //    priority=rand(); // necessary???
    //    clear_on_left_path();
#if DEBUG_TREAP >0
    cout<<"treap::insert_left("<<((left_most)?left_most->node_name():"")<<")"<<endl;
#endif
#if DEBUG_TREAP >0
    cout<<"  priority="<<priority<<endl;
#endif
#if DEBUG_TREAP >1
    daVinci_connection.add_node(node_name().c_str(),"treap_node",
				daVinci_node_attributes().c_str());
#endif
    if (left_most==0) {
      set_on_left_path();
      set_rank(0);
#if DEBUG_TREAP >0
    cout<<"  on_left_path="<<is_on_left_path()<<endl;
#endif
    check_tree();
      return;
    }
    assert(left_most->is_on_left_path());
    if (left_most->is_on_left_path())
      set_on_left_path();
    else
      clear_on_left_path();
#if DEBUG_TREAP >0
    cout<<"  on_left_path="<<is_on_left_path()<<endl;
#endif
    assert(left_most->left == 0);
    left_most->left = this;
    parent = left_most;
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_edge((left_most->node_name()+"_"+node_name()+"l").c_str(),
        "left_edge", "[]", left_most->node_name().c_str(), node_name().c_str());
      daVinci_connection.add_edge((node_name()+"_"+left_most->node_name()+"p").c_str(),
        "parent_edge", "[]", node_name().c_str(), left_most->node_name().c_str());
      //      daVinci_connection.wait_for_menu_selection("next");
    }
#endif
    set_rank(0);
#if DEBUG_TREAP >0
    cout<<"  rank="<<get_rank()<<endl;
#endif
    //adjust rank values of parent nodes
    treap* old_p = this;
    for(treap* p=parent;p!=0; old_p=p, p=p->parent)
    {
      if (old_p == p->left) {
        p->increase_rank();
#if DEBUG_TREAP >1
      daVinci_connection.change_node_attributes(p->node_name().c_str(),p->daVinci_node_attributes().c_str());
#endif
      }
    }
#if DEBUG_TREAP >1
      daVinci_connection.change_node_attributes(node_name().c_str(),daVinci_node_attributes().c_str());
#endif
    assert(left==0 && right==0);
    rdparent->left_most_treap=this;
    check_tree();
    rotate_up();
    check_tree();
  }

protected:
  /*inline*/ unsigned long long get_rank_in_tree(bool move_down) {
    check_tree();
    if (this==rdparent->left_most_treap)
      return 0;
#if DEBUG_TREAP >0
    cout<<"treap::get_rank_in_tree("<<node_name()<<")"<<endl;
#endif
    unsigned long long rank=get_rank();
#define NEW_UPDATE_ALGORITHM
#ifdef NEW_UPDATE_ALGORITHM
    /* trying a smarter and faster implementation of
       the update operation.
       KB 24.04.2001
    */

#ifndef NDEBUG
    tree_consistent=false;
#endif
    treap* old_p = this;
    treap *p=0, *first_parent_of_new_and_old_place=0;
    // first decrease 
    if (!is_on_left_path() && parent && parent->right==this)
      rank+= 1+parent->get_rank();
    if (parent!=0)
      for(p=parent;!p->is_on_left_path(); old_p=p, p=p->parent)
	{
	  if (p->parent->right == p)
	    rank+=1+p->parent->get_rank();
	  if (old_p == p->left)
	    p->decrease_rank();
	}
    // When the treap-node will be moved from the right subtree
    // of the first_parent_of_new_and_old_place-node, to the
    // left subtree (it is always moved to the left subtree),
    // there will finally be one more node in the
    // first_parent_of_new_and_old_place's left subtree.
    // So, in this case we need to increase the rank of
    // the parent node by 1.
    if (p!=0 && old_p == p->right)
      p->increase_rank();
    first_parent_of_new_and_old_place=p;
      
      

    if (move_down) {
      // now move the this node down, and correct the rank
      // on the fly if it is rotated into the left subtree.

      while (left!=0 || right!=0) {
        if (left==0) {
          //	right->rank++;
	  right->rotate();
	  parent->decrease_rank();
        }
        else if (right==0) {
	  left->rotate();
        } else if (left->priority > right->priority) {
	  left->rotate();
        } else {
	  //	right->rank++;
	  right->rotate();
	  parent->decrease_rank();
        }
      }

      // remove the node from the tree
      if (parent!=0)
        {
	  if (parent->left==this)
          {
	    parent->left=0; 
          }
	  else {
	    assert(parent->right==this);
	    parent->right=0;
	  }
        }
      /*parent=*/left=right=0;
      check_tree();

      // now insert the node back to the left of the left_most node
      assert(rdparent->left_most_treap->is_on_left_path());
      set_on_left_path();
      assert(rdparent->left_most_treap->left == 0);
      assert(this!=rdparent->left_most_treap);
      /*
      if (this!=left_most_treap)
        {
      */
      rdparent->left_most_treap->left = this;
      parent = rdparent->left_most_treap;
      /*
        }
      */
      set_rank(0);
      // adjust rank values of parent nodes
      // up until the first_parent_of_new_and_old_place node
      old_p = this;
      for(treap* p=parent;p!=first_parent_of_new_and_old_place;
	  old_p=p, p=p->parent)
      {
        if (old_p == p->left) {
          p->increase_rank();
        }
      }
      /*
      if (0!=first_parent_of_new_and_old_place)
        {
	  assert(first_parent_of_new_and_old_place->is_on_left_path());
	  first_parent_of_new_and_old_place->increase_rank();
        }
      */

      assert(left==0 && right==0);
      rdparent->left_most_treap=this;
#ifndef NDEBUG
      tree_consistent=true;
#endif
      check_tree();
      rotate_up();
      check_tree();

      /*
      while(!is_on_left_path())
        {
	  assert(parent!=0);
	  if (parent->right==this)
	    rank+= 1 + parent->get_rank();
	  rotate();
        }
      */
#else
      for(p=this; !p->is_on_left_path(); p=p->parent) {
        assert(p->parent!=0);
        if (p->parent->left==p) {
	  /*rank++;*/
        } else {
	  assert(p->parent->right==p);
	  rank+= 1 + p->parent->get_rank();
        }
        //rotate();
      }
      treap* left_most=p;
      while(left_most->left != 0) left_most=left_most->left;
      if (left_most!=this) {
        remove();
        insert_left(left_most);
      }
      check_tree();
#endif
    } // if (move_down)

    return rank;
  }

#if DEBUG_TREAP>1
  void print_vcg_graph(string filename, treap* root) const {
    ofstream vcg_graph((filename+".vcg").c_str());
  }
  string daVinci_node_attributes() const {
    string s="[a(\"OBJECT\",\"";
    s+=string("p = ")+node_name().c_str()+"\\n";
    s+=string("on_left_path = ")+(is_on_left_path()?"true":"false")+"\\n";
      char rank_string[40];
      sprintf(rank_string,"%lld",get_rank());
    s+=string("rank = ")+rank_string+"\\n";
      char priority_string[40];
      sprintf(priority_string,"%d",priority);
    s+=string("priority = ")+priority_string;
    s+="\")";
    s+="]"; 
    return s;
  }

  static daVinci daVinci_connection;
#endif



public:

  treap* find_root() {
    treap* root;
    for(root=this; root->parent!=0; root=root->parent) ;
    return root;
  }

  string in_order_tree() {
    string result;
    if (left) result+=left->in_order_tree();
    result+=node_name()+" ";
    if (right) result+=right->in_order_tree();
    return result;
  }

  inline unsigned long calculate_rank() const {
    unsigned long rank=0;
    for(treap* p=left; p!=0; p=p->right)
      rank += p->rank+1;
    return rank;
  }

  inline void check_node() {
#ifndef NDEBUG
    assert(parent!=0); 
    assert(parent->left == this || parent->right==this);
    if (parent->is_on_left_path() && parent->left==this)
       assert(is_on_left_path());
    if (parent->left==this)
       assert(get_rank() < parent->get_rank());
    assert(calculate_rank()==rank);
    if (left!=0) left->check_node();
    if (right!=0) right->check_node();
#endif
  }

  /*  treap(unsigned int _node_nr) : left(0), right(0), priority(rand()),
      node_nr(_node_nr), info(0ULL)  {}*/
  rd * rdparent;
  treap(const void * const _addr, rd * _rdparent, unsigned int *seedp)
    : left(0), right(0), parent(0), priority(rand_r(seedp)),
    address(_addr), last_array_ref(-1)/*, next_in_hash(0)*/, rank(0UL), on_left_flag(false),
    rdparent(_rdparent)
  {}

  /** assuming that right_most is the node with the highest in-order
      ordering, add this node with higher in-order ordering */
  inline void insert_right(treap* right_most) {
    check_tree();
    //priority=rand();
#if DEBUG_TREAP >0
    cout<<"treap::insert_right("<<((right_most!=0)?right_most->node_name():"")<<")"<<endl;
#endif
    clear_on_left_path();
#if DEBUG_TREAP >0
    cout<<"  priority="<<priority<<endl;
#endif
    if (right_most==0) {
      set_on_left_path();
#if DEBUG_TREAP >0
    cout<<"  on_left_path="<<is_on_left_path()<<endl;
#endif
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_node(node_name().c_str(),"treap_node",
	daVinci_node_attributes().c_str());
      daVinci_connection.wait_for_menu_selection("next");
    }
#endif
      return;
    }
#if DEBUG_TREAP >0
    cout<<"  on_left_path="<<is_on_left_path()<<endl;
#endif
    assert(right_most->right == 0);
    right_most->right = this;
    parent = right_most;
    set_rank(0);
#if DEBUG_TREAP >0
    cout<<"  rank="<<get_rank()<<endl;
#endif
    assert(left==0 && right==0);
#if DEBUG_TREAP >1
    {
      daVinci_connection.add_node(node_name().c_str(),"treap_node",
	daVinci_node_attributes().c_str());
      daVinci_connection.add_edge((right_most->node_name()+"_"+node_name()+"r").c_str(),
        "right_edge", "[]", right_most->node_name().c_str(), node_name().c_str());
      daVinci_connection.add_edge((node_name()+"_"+right_most->node_name()+"p").c_str(),
        "parent_edge", "[]", node_name().c_str(), right_most->node_name().c_str());
      daVinci_connection.wait_for_menu_selection("next");
    }
#endif
    rotate_up();
    check_tree();
  }



  ~treap() {
#if DEBUG_TREAP >1
    {
      daVinci_connection.delete_node(node_name().c_str());
    }
#endif
remove();
  }

public:

};

inline treap* rd::lookup_addr(const void* const addr) {
  // make it into a tree to make the lookup more efficient
  //unsigned index = hash_bucket(addr);
  return hash_table.find(addr);
/*
  treap* result = hash_table[hash_bucket(addr)];
  while (result!=0 && result->address != addr)
    result=result->next_in_hash;
  return result;
*/
}


/* reference address `addr', moving it to the top of the stack, and return its reuse distance */

pair<unsigned long, long long> rd::reference(const void* const addr, long long current_array_ref, unsigned int *seedp) {
  /*fprintf(stderr,"treap::reference(%x)",addr);*/
  pair<unsigned long, long long> result;
  treap* t=lookup_addr(addr);
  if (t==0) {
    t = new treap(addr, this, seedp);
    if (first_treap==0) {
      t->insert_left(0);
      first_treap=t;
      left_most_treap=t;
    }
    else
    {
      //treap* left_most=first_treap->find_root();
      //while (left_most->left!=0) 
      //  left_most=left_most->left;
      t->insert_left(left_most_treap);
    }
    t->add_to_hash_table();
    result.first = (unsigned long)-1;
    result.second = -1;
  }
  else {
    result.first = t->get_rank_in_tree(true);
    result.second = t->last_array_ref;
  }
  t->last_array_ref = current_array_ref;
  return result;
}


/* return distance of address `addr', without referencing it */

unsigned long rd::distance(const void* const addr) {
  treap* t=lookup_addr(addr);
  if (t==0)
    return (unsigned long)-1;
  else
    return t->get_rank_in_tree(false);
}


#endif
