#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/syscall.h> /*必须引用这个文件 */
 
static int nthread = 1;
static int round = 0;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
  // int ndone;
} bstate;
pthread_mutex_t lock;
// pthread_mutex_t lock_[1000];

pid_t gettid(void)
{
	return syscall(SYS_gettid);
}


static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  // printf("barrier begin!\n");
  pthread_mutex_lock(&bstate.barrier_mutex);
  //pthread_mutex_lock(&lock);
  bstate.nthread++;
  
  // pthread_mutex_lock(&lock_[bstate.nthread]);

  if(bstate.nthread == nthread){
    // printf("%d awake!\n", gettid());
    // pthread_mutex_lock(&lock);
    bstate.round++;
    bstate.nthread = 0;
    // bstate.ndone++;
    //pthread_mutex_unlock(&lock);
    pthread_cond_broadcast(&bstate.barrier_cond);  // 唤醒睡在cond的所有线程;
    //printf("barrier: %d\n", bstate.round);
    // pthread_mutex_lock(&lock);
  }

  else if(bstate.nthread < nthread) {
    // pthread_mutex_unlock(&lock);
    // printf("%d wait!\n", gettid());
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);// 等待时释放锁, 醒来后重新获取;
    //pthread_mutex_lock(&lock);
    // bstate.ndone++;
    //pthread_mutex_unlock(&lock);
    // pthread_mutex_lock(&lock);
  }
  // printf("barrier end!\n");
  pthread_mutex_unlock(&bstate.barrier_mutex);
  //pthread_mutex_unlock(&lock);
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    //printf("%d %d %d\n",gettid(), i, t);

    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  // printf("go out!\n");
  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  // printf("create done!\n");

  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }

  // printf("go here!\n");
  printf("OK; passed\n");
}
