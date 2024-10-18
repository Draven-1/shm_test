#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <signal.h>

typedef struct {
    int id;
    double value;
} DataToB;  // A进程写入，B进程读取的数据结构

typedef struct {
    int status;
    float result;
} DataFromB;  // B进程写入，A进程读取的数据结构

#define SHM_A_TO_B "/shm_a_to_b"  // A写B读的共享内存标识
#define SHM_B_TO_A "/shm_b_to_a"  // B写A读的共享内存标识

#define SEM_WRITE_TO_B "/sem_write_to_b"
#define SEM_WRITE_TO_A "/sem_write_to_a"

// 全局变量以便信号处理器访问
sem_t *sem_write_to_b;
sem_t *sem_write_to_a;

void *ptr_a_to_b;
void *ptr_b_to_a;

void clean_up() {
    printf("Cleaning up resources...\n");

    munmap(ptr_a_to_b, sizeof(DataToB));
    munmap(ptr_b_to_a, sizeof(DataFromB));

    sem_close(sem_write_to_b);
    sem_close(sem_write_to_a);

    // 不删除共享内存文件和信号量文件
}

void signal_handler(int signal) {
    if (signal == SIGINT) {
        clean_up();
        exit(0);
    }
}

int main() {
    signal(SIGINT, signal_handler);

    // 创建或打开A到B的共享内存，A写入
    int shm_fd_a_to_b = shm_open(SHM_A_TO_B, O_CREAT | O_RDWR, 0666); // O_CREAT: 如果不存在则创建, O_RDWR: 打开为读写模式, 0666: 文件权限, 允许用户和组读写
    if (shm_fd_a_to_b == -1) {
        perror("Error opening shm A to B");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd_a_to_b, sizeof(DataToB)) == -1) { // ftruncate: 设置共享内存段的大小
        perror("Error setting size of shm A to B");
        exit(EXIT_FAILURE);
    }
    ptr_a_to_b = mmap(0, sizeof(DataToB), PROT_WRITE, MAP_SHARED, shm_fd_a_to_b, 0); // mmap: 映射内存段, PROT_WRITE: 映射区域为只写, MAP_SHARED: 修改对所有进程可见
    if (ptr_a_to_b == MAP_FAILED) {
        perror("Error mapping shm A to B");
        exit(EXIT_FAILURE);
    }

    memset(ptr_a_to_b, 0, sizeof(DataToB));  // 初始化内存区域

    // 打开B到A的共享内存，A读取
    int shm_fd_b_to_a = shm_open(SHM_B_TO_A, O_RDWR, 0666); // O_RDWR: 打开为读写模式
    if (shm_fd_b_to_a == -1) {
        perror("Error opening shm B to A");
        exit(EXIT_FAILURE);
    }

    ptr_b_to_a = mmap(0, sizeof(DataFromB), PROT_READ, MAP_SHARED, shm_fd_b_to_a, 0); // PROT_READ: 映射区域为只读
    if (ptr_b_to_a == MAP_FAILED) {
        perror("Error mapping shm B to A");
        exit(EXIT_FAILURE);
    }

    // 关闭文件描述符
    close(shm_fd_a_to_b);
    close(shm_fd_b_to_a);

    DataToB *dataToB = (DataToB *)ptr_a_to_b;
    DataFromB *dataFromB = (DataFromB *)ptr_b_to_a;

    // 打开或创建信号量SEM_A
    // O_CREAT: 如果信号量不存在，则创建它
    // 0666: 设置信号量的权限，允许用户和组读写
    // 1: 信号量的初始值设为1，允许一个进程进入临界区
    // 初始化信号量，B进程读取完成后才能写入
    sem_write_to_b = sem_open(SEM_WRITE_TO_B, O_CREAT, 0666, 0);
    sem_write_to_a = sem_open(SEM_WRITE_TO_A, O_CREAT, 0666, 1);

    struct timespec ts = {0, 7000000};  // 7毫秒的时间间隔

    while (1) {
        #if 0  // 信号量为阻塞式的
        // 写AToB的数据
        dataToB->id++;
        dataToB->value += 1.0;
        printf("Send To B = id: %d, value = %f\n", dataToB->id, dataToB->value);
        sem_post(sem_write_to_b);  // Signal B that data is ready in AToB  +1
        
        // 读BToA的数据
        sem_wait(sem_write_to_a);  // Wait for B to write to BToA  -1
        printf("Received from B: status = %d, result = %f\n", dataFromB->status, dataFromB->result);
        #endif

        #if 1  // 信号量为非阻塞式的  读失败，写的数据就不更新
        if (sem_trywait(sem_write_to_a) == 0) {
            // 成功获取信号量，处理数据
            printf("Received from B: status = %d, result = %f\n", dataFromB->status, dataFromB->result);

            // 更新数据，因为确认B已经读取了之前的数据
            dataToB->id++;
            dataToB->value += 1.0;
            printf("Send To B = id: %d, value = %f\n", dataToB->id, dataToB->value);
            sem_post(sem_write_to_b);
        } else {
            // 处理信号量不可用的情况
            printf("Waiting B process write Data\n");
        }
        #endif

        #if 0  // 信号量为非阻塞式的  读失败，写的数据也在更新；会导致信号量增加的很大，另一个进程退出，还得读一段时间
        // 写AToB的数据
        dataToB->id++;
        dataToB->value += 1.0;
        printf("Send To B = id: %d, value = %f\n", dataToB->id, dataToB->value);
        sem_post(sem_write_to_b);  // 通知B数据已准备好

        // 尝试从BToA读取数据
        if (sem_trywait(sem_write_to_a) == 0) {
            // 成功获取信号量，读取数据
            printf("Received from B: status = %d, result = %f\n", dataFromB->status, dataFromB->result);
        } else {
            // 信号量不可用，处理非阻塞失败情况
            printf("Data from B is not ready yet\n");
            // 可以选择稍后重试或执行其他操作
        }
        #endif

        nanosleep(&ts, NULL); // 暂停以模拟处理时间
    }

    return 0;
}

