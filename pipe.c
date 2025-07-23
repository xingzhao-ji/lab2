#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

static int map(int s){
    if (WIFSIGNALED(s)) return WTERMSIG(s)==SIGPIPE?0:128+WTERMSIG(s);
    int c=WEXITSTATUS(s); return c==128+SIGPIPE?0:c;
}

static int looks_cmd(const char *t){
    if(!t||!*t) return 0;                
    if(strlen(t)==1) return 0;           
    if(t[0]=='-'        ) return 0;     
    if(isdigit((unsigned char)t[0])) return 0;
    for(const char *p=t;*p;++p)
        if(!isalnum((unsigned char)*p) && *p!='_' && *p!='/') return 0;
    return 1;                          
}


int main(int argc,char*argv[]){
    if(argc<=1) exit(EINVAL);

    int *st=malloc((argc-1)*sizeof(int));
    int *ln=malloc((argc-1)*sizeof(int));
    if(!st||!ln) exit(ENOMEM);

    int m=0,i=1;
    while(i<argc){
        st[m]=i; ln[m]=1; ++i;
        while(i<argc && !looks_cmd(argv[i])){ ++ln[m]; ++i; }
        ++m;
    }

    pid_t *pid=malloc(m*sizeof(pid_t));
    int *code=malloc(m*sizeof(int));
    if(!pid||!code) exit(ENOMEM);
    for(i=0;i<m;++i) code[i]=-1;

    int prev=STDIN_FILENO;
    for(i=0;i<m;++i){
        int fds[2];
        if(i!=m-1 && pipe(fds)==-1) exit(errno);
        pid_t p=fork();
        if(p==-1) exit(errno);

        if(!p){
            if(prev!=STDIN_FILENO && dup2(prev,STDIN_FILENO)==-1) _exit(errno);
            if(i!=m-1 && dup2(fds[1],STDOUT_FILENO)==-1) _exit(errno);
            if(prev!=STDIN_FILENO) close(prev);
            if(i!=m-1){ close(fds[0]); close(fds[1]); }

            char **v=malloc((ln[i]+1)*sizeof(char*));
            if(!v) _exit(ENOMEM);
            for(int k=0;k<ln[i];++k) v[k]=argv[st[i]+k];
            v[ln[i]]=NULL;
            execvp(v[0],v);
            perror(v[0]); _exit(errno);
        }

        pid[i]=p;
        if(prev!=STDIN_FILENO) close(prev);
        if(i!=m-1){ close(fds[1]); prev=fds[0]; }
    }

    for(int left=m;left;){
        int s; pid_t w=wait(&s);
        if(w==-1){ if(errno==EINTR) continue; exit(errno); }
        for(int k=0;k<m;++k) if(pid[k]==w){ code[k]=map(s); break; }
        --left;
    }

    int rc=0;
    for(i=0;i<m;++i) if(code[i]){ rc=code[i]; break; }
    return rc;
}
