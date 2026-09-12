#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "npfs.h"
#include "npfsimpl.h"

static struct Npusercache {
	pthread_mutex_t lock;
	Npuser* users;
} usercache = { PTHREAD_MUTEX_INITIALIZER, 0 };

static struct Npgroupcache {
	pthread_mutex_t lock;
	Npgroup* groups;
} groupcache = { PTHREAD_MUTEX_INITIALIZER, 0 };

static Npuser *
np_simpl_uname2user(Npuserpool *up, char *uname)
{
	Npuser *u;

	pthread_mutex_lock(&usercache.lock);
	for(u = usercache.users; u; u = u->next) {
		if(strcmp(u->uname, uname) == 0)
			break;
	}
	if(!u) {
		u = np_malloc(sizeof(*u) + strlen(uname) + 1);
		pthread_mutex_init(&u->lock, NULL);
		u->refcount = 1;
		u->upool = up;
		u->uid = -1;
		u->uname = (char *)u + sizeof(*u);
		strcpy(u->uname, uname);
		u->dfltgroup = NULL;
		u->ngroups = 0;
		u->groups = NULL;
		u->next = NULL;
		u->dfltgroup = (*up->gname2group)(up, uname);

		u->next = usercache.users;
		usercache.users = u;
	}
	np_user_incref(u);
	pthread_mutex_unlock(&usercache.lock);
	return u;
}

static Npuser *
np_simpl_uid2user(Npuserpool *up, u32 uid)
{
	Npuser *u;
	char uname[32];

	/* No name service stands behind this pool, so an id names itself.
	 * 9P2000.L carries ids numerically and a server that reports them
	 * that way needs no other answer. Entries the name side made carry
	 * no id, so they cannot answer here and are passed over. */
	snprintf(uname, sizeof(uname), "%u", uid);

	pthread_mutex_lock(&usercache.lock);
	for(u = usercache.users; u; u = u->next) {
		if(u->uid != (uid_t) -1 && u->uid == uid)
			break;
	}
	if(!u) {
		u = np_malloc(sizeof(*u) + strlen(uname) + 1);
		pthread_mutex_init(&u->lock, NULL);
		u->refcount = 1;
		u->upool = up;
		u->uid = uid;
		u->uname = (char *)u + sizeof(*u);
		strcpy(u->uname, uname);
		u->dfltgroup = NULL;
		u->ngroups = 0;
		u->groups = NULL;
		u->next = NULL;
		u->dfltgroup = (*up->gname2group)(up, uname);

		u->next = usercache.users;
		usercache.users = u;
	}
	np_user_incref(u);
	pthread_mutex_unlock(&usercache.lock);
	return u;
}

static Npgroup *
np_simpl_gname2group(Npuserpool *up, char *gname)
{
	Npgroup *g;

	pthread_mutex_lock(&groupcache.lock);
	for(g = groupcache.groups; g; g = g->next) {
		if(strcmp(g->gname, gname) == 0)
			break;
	}

	if(!g) {
		g = np_malloc(sizeof(*g) + strlen(gname) + 1);
		pthread_mutex_init(&g->lock, NULL);
		g->refcount = 1;
		g->upool = up;
		g->gid = -1;
		g->gname = (char *)g + sizeof(*g);
		strcpy(g->gname, gname);

		g->next = groupcache.groups;
		groupcache.groups = g;
	}
	np_group_incref(g);
	pthread_mutex_unlock(&groupcache.lock);
	return g;
}

static Npgroup *
np_simpl_gid2group(Npuserpool *up, u32 gid)
{
	return NULL;
}

static int
np_simpl_ismember(Npuserpool *up, Npuser *u, Npgroup *g)
{
	return 0; // XXX something fancier?
}

static void
np_simpl_udestroy(Npuserpool *up, Npuser *u)
{
}

static void
np_simpl_gdestroy(Npuserpool *up, Npgroup *g)
{
}

static Npuserpool upool = {
	NULL,
	np_simpl_uname2user,
	np_simpl_uid2user,
	np_simpl_gname2group,
	np_simpl_gid2group,
	np_simpl_ismember,
	np_simpl_udestroy,
	np_simpl_gdestroy,
};

Npuserpool *np_simpl_users = &upool;
