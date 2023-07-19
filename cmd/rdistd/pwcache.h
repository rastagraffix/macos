#ifndef _PWCACHE_H
# define _PWCACHE_H

char *group_from_gid (gid_t, int);
char  *user_from_uid (uid_t, int);

int  uid_from_user (const char *, uid_t *);
int gid_from_group (const char *, gid_t *);

#define _GR_BUF_LEN (1024+200*sizeof(char *))
#define _PW_BUF_LEN 1024

#endif /* _PWCACHE_H */
