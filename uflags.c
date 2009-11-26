/*
 * Copyright (C) 2009 Thiago Rondon
 * Copyright (c) 2009 Aware
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 *
 * TODO:
 * -----
 *
 *
 * uflags module: global user flags; it keeps a bitmap of flags
 * in shared memory and may be used to change behaviour 
 * of server based on value of the flags. 
 *
 */


/* flag buffer size for FIFO protocool */
#define MAX_FLAG_LEN 12
/* FIFO action protocol names */
#define FIFO_SET_GFLAG "set_uflag"
#define FIFO_IS_GFLAG "is_uflag"
#define FIFO_RESET_GFLAG "reset_uflag"
#define FIFO_GET_GFLAGS "get_uflags"

#include <stdio.h>
#include "../../sr_module.h"
#include "../../ut.h"
#include "../../mem/mem.h"
#include "../../mem/shm_mem.h"
#include "../../mi/mi.h"



static int set_uflag(struct sip_msg*, char *, char *);
static int reset_uflag(struct sip_msg*, char *, char *);
static int is_uflag(struct sip_msg*, char *, char *);

static struct mi_root* mi_set_uflag(struct mi_root* cmd, void* param );
static struct mi_root* mi_reset_uflag(struct mi_root* cmd, void* param );
static struct mi_root* mi_is_uflag(struct mi_root* cmd, void* param );
static struct mi_root* mi_get_uflags(struct mi_root* cmd, void* param );

static int fixup_uflags( void** param, int param_no);

static int  mod_init(void);
static void mod_destroy(void);

static int initial=0;
static unsigned int *uflags=0;

static cmd_export_t cmds[]={
	{"set_uflag",    (cmd_function)set_uflag,   1,   fixup_uflags, 0,
		REQUEST_ROUTE|FAILURE_ROUTE|ONREPLY_ROUTE|BRANCH_ROUTE|LOCAL_ROUTE|
		STARTUP_ROUTE|TIMER_ROUTE},
	{"reset_uflag",  (cmd_function)reset_uflag, 1,   fixup_uflags, 0,
		REQUEST_ROUTE|FAILURE_ROUTE|ONREPLY_ROUTE|BRANCH_ROUTE|LOCAL_ROUTE|
		STARTUP_ROUTE|TIMER_ROUTE},
	{"is_uflag",     (cmd_function)is_uflag,    1,   fixup_uflags, 0,
		REQUEST_ROUTE|FAILURE_ROUTE|ONREPLY_ROUTE|BRANCH_ROUTE|LOCAL_ROUTE|
		STARTUP_ROUTE|TIMER_ROUTE},
	{0, 0, 0, 0, 0, 0}
};

static param_export_t params[]={
	{"initial", INT_PARAM, &initial},
	{0,0,0} 
};

static mi_export_t mi_cmds[] = {
	{ FIFO_SET_GFLAG,   mi_set_uflag,   0,                 0,  0 },
	{ FIFO_RESET_GFLAG, mi_reset_uflag, 0,                 0,  0 },
	{ FIFO_IS_GFLAG,    mi_is_uflag,    0,                 0,  0 },
	{ FIFO_GET_GFLAGS,  mi_get_uflags,  MI_NO_INPUT_FLAG,  0,  0 },
	{ 0, 0, 0, 0, 0}
};

struct module_exports exports = {
	"uflags",
	MODULE_VERSION,
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,        /* exported functions */
	params,      /* exported parameters */
	0,           /* exported statistics */
	mi_cmds,     /* exported MI functions */
	0,           /* exported pseudo-variables */
	0,           /* extra processes */
	mod_init,    /* module initialization function */
	0,           /* response function*/
	mod_destroy, /* destroy function */
	0            /* per-child init function */
};


/**************************** fixup functions ******************************/
/**
 * convert char* to int and do bitwise right-shift
 * char* must be pkg_alloced and will be freed by the function
 */
static int fixup_uflags( void** param, int param_no)
{
	unsigned int myint;
	str param_str;

	/* we only fix the parameter #1 */
	if (param_no!=1)
		return 0;

	param_str.s=(char*) *param;
	param_str.len=strlen(param_str.s);

	if (str2int(&param_str, &myint )<0) {
		LM_ERR("bad number <%s>\n", (char *)(*param));
		return E_CFG;
	}
	if ( myint >= 8*sizeof(*uflags) ) {
		LM_ERR("flag <%d> out of "
			"range [0..%lu]\n", myint, ((unsigned long)8*sizeof(*uflags))-1 );
		return E_CFG;
	}
	/* convert from flag index to flag bitmap */
	myint = 1 << myint;
	/* success -- change to int */
	pkg_free(*param);
	*param=(void *)(long)myint;
	return 0;
}



/**************************** module functions ******************************/

static int set_uflag(struct sip_msg *bar, char *flag, char *foo) 
{
	(*uflags) |= (unsigned int)(long)flag;
	return 1;
}


static int reset_uflag(struct sip_msg *bar, char *flag, char *foo)
{
	(*uflags) &= ~ ((unsigned int)(long)flag);
	return 1;
}


static int is_uflag(struct sip_msg *bar, char *flag, char *foo)
{
	return ( (*uflags) & ((unsigned int)(long)flag)) ? 1 : -1;
}


/************************* MI functions *******************************/

static struct mi_root* mi_set_uflag(struct mi_root* cmd_tree, void* param )
{
	unsigned int flag;
	struct mi_node* node;

	node = cmd_tree->node.kids;
	if(node == NULL)
		return init_mi_tree( 400, MI_MISSING_PARM_S, MI_MISSING_PARM_LEN);

	flag = 0;
	if( strno2int( &node->value, &flag) <0)
		goto error;
	if (!flag) {
		LM_ERR("incorrect flag\n");
		goto error;
	}

	(*uflags) |= flag;

	return init_mi_tree( 200, MI_OK_S, MI_OK_LEN);
error:
	return init_mi_tree( 400, MI_BAD_PARM_S, MI_BAD_PARM_LEN);
}



static struct mi_root*  mi_reset_uflag(struct mi_root* cmd_tree, void* param )
{
	unsigned int flag;
	struct mi_node* node = NULL;

	node = cmd_tree->node.kids;
	if(node == NULL)
		return init_mi_tree( 400, MI_MISSING_PARM_S, MI_MISSING_PARM_LEN);

	flag = 0;
	if( strno2int( &node->value, &flag) <0)
		goto error;
	if (!flag) {
		LM_ERR("incorrect flag\n");
		goto error;
	}

	(*uflags) &= ~ flag;

	return init_mi_tree( 200, MI_OK_S, MI_OK_LEN);
error:
	return init_mi_tree( 400, MI_BAD_PARM_S, MI_BAD_PARM_LEN);
}



static struct mi_root* mi_is_uflag(struct mi_root* cmd_tree, void* param )
{
	unsigned int flag;
	struct mi_root* rpl_tree = NULL;
	struct mi_node* node = NULL;

	node = cmd_tree->node.kids;
	if(node == NULL)
		return init_mi_tree( 400, MI_MISSING_PARM_S, MI_MISSING_PARM_LEN);

	flag = 0;
	if( strno2int( &node->value, &flag) <0)
		goto error_param;
	if (!flag) {
		LM_ERR("incorrect flag\n");
		goto error_param;
	}

	rpl_tree = init_mi_tree( 200, MI_OK_S, MI_OK_LEN);
	if(rpl_tree ==0)
		return 0;

	if( ((*uflags) & flag)== flag )
		node = add_mi_node_child( &rpl_tree->node, 0, 0, 0, "TRUE", 4);
	else
		node = add_mi_node_child( &rpl_tree->node, 0, 0, 0, "FALSE", 5);

	if(node == NULL)
	{
		LM_ERR("failed to add node\n");
		free_mi_tree(rpl_tree);
		return 0;
	}

	return rpl_tree;
error_param:
	return init_mi_tree( 400, MI_BAD_PARM_S, MI_BAD_PARM_LEN);
}


static struct mi_root*  mi_get_uflags(struct mi_root* cmd_tree, void* param )
{
	struct mi_root* rpl_tree= NULL;
	struct mi_node* node= NULL;

	rpl_tree = init_mi_tree( 200, MI_OK_S, MI_OK_LEN );
	if(rpl_tree == NULL)
		return 0;

	node = addf_mi_node_child( &rpl_tree->node, 0, 0, 0, "0x%X",(*uflags));
	if(node == NULL)
		goto error;

	node = addf_mi_node_child( &rpl_tree->node, 0, 0, 0, "%u",(*uflags));
	if(node == NULL)
		goto error;

	return rpl_tree;
error:
	free_mi_tree(rpl_tree);
	return 0;
}



static int mod_init(void)
{
	uflags=(unsigned int *) shm_malloc(sizeof(unsigned int));
	if (!uflags) {
		LM_ERR(" no shmem\n");
		return -1;
	}
	*uflags=initial;
	return 0;
}


static void mod_destroy(void)
{
	if (uflags)
		shm_free(uflags);
}

