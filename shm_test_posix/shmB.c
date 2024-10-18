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
} DataFromA;  // A进程写入，B进程读取的数据结构

typedef struct {
    int status;
    float result;
} DataToA;  // B进程写入，A进程读取的数据结构

#define SHM_A_TO_B "/shm_a_to_b"
#define SHM_B_TO_A "/shm_b_to_a"

#define SEM_WRITE_TO_B "/sem_write_to_b"
#define SEM_WRITE_TO_A "/sem_write_to_a"

// 全局变量以便信号处理器访问
sem_t *sem_write_to_b;
sem_t *sem_write_to_a;

void *ptr_b_to_a;
void *ptr_a_to_b;

void clean_up() {
    printf("Cleaning up resources...\n");

    munmap(ptr_b_to_a, sizeof(DataToA));
    munmap(ptr_a_to_b, sizeof(DataFromA));

#if 0  // 会导致程序下次启动后信号量还是0
    int semVal;
    sem_getvalue(sem_write_to_b, &semVal);  // 获取to_b信号量计数
    while (semVal > 0) {
        sem_wait(sem_write_to_b);
        sem_getvalue(sem_write_to_b, &semVal);
        printf("程序退出前，to_b信号量计数清零，当前计数: %d\n", semVal);
    }
#endif

    sem_close(sem_write_to_a);
    sem_close(sem_write_to_b);

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

    // 打开A到B的共享内存，B读取
    int shm_fd_a_to_b = shm_open(SHM_A_TO_B, O_RDWR, 0666); // O_RDWR: 打开为读写模式
    if (shm_fd_a_to_b == -1) {
        perror("Error opening shm A to B");
        exit(EXIT_FAILURE);
    }
    ptr_a_to_b = mmap(0, sizeof(DataFromA), PROT_READ, MAP_SHARED, shm_fd_a_to_b, 0); // PROT_READ: 映射区域为只读
    if (ptr_a_to_b == MAP_FAILED) {
        perror("Error mapping shm A to B");
        exit(EXIT_FAILURE);
    }

    // 创建或打开B到A的共享内存，B写入
    int shm_fd_b_to_a = shm_open(SHM_B_TO_A, O_CREAT | O_RDWR, 0666); // O_CREAT: 如果不存在则创建, O_RDWR: 打开为读写模式
    if (shm_fd_b_to_a == -1) {
        perror("Error opening shm B to A");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd_b_to_a, sizeof(DataToA)) == -1) {
        perror("Error setting size of shm B to A");
        exit(EXIT_FAILURE);
    }


    ptr_b_to_a = mmap(0, sizeof(DataToA), PROT_WRITE, MAP_SHARED, shm_fd_b_to_a, 0); // PROT_WRITE: 映射区域为只写
    if (ptr_b_to_a == MAP_FAILED) {
        perror("Error mapping shm B to A");
        exit(EXIT_FAILURE);
    }

    memset(ptr_b_to_a, 0, sizeof(DataToA));  // 初始化内存区域

    // 关闭文件描述符
    close(shm_fd_a_to_b);
    close(shm_fd_b_to_a);

    DataFromA *dataFromA = (DataFromA *)ptr_a_to_b;
    DataToA *dataToA = (DataToA *)ptr_b_to_a;

    // 初始化信号量
    sem_write_to_b = sem_open(SEM_WRITE_TO_B, 0);
    sem_write_to_a = sem_open(SEM_WRITE_TO_A, 0);

    struct timespec ts = {0, 7000000};  // 设置7毫秒的时间间隔

    while (1) {
        #if 0  // 信号量为阻塞式的
        // 读AToB的数据
        sem_wait(sem_write_to_b);  // Wait for A to write to AToB  -1
        printf("Received from A: id = %d, value = %f\n", dataFromA->id, dataFromA->value);

        // 写BToA的数据
        dataToA->status++;    // 状态设置为A进程传来的id值
        dataToA->result += 1.0;             // 结果值累加
        printf("Send To A: status = %d, result = %f\n", dataToA->status, dataToA->result);
        sem_post(sem_write_to_a);  // Signal A that data is ready in BToA  +1
        #endif

        #if 1  // 信号量为非阻塞式的   读失败，写的数据就不更新
        if (sem_trywait(sem_write_to_b) == 0) {
            // 成功获取信号量，读取数据
            printf("Received from A: id = %d, value = %f\n", dataFromA->id, dataFromA->value);

            // 更新BToA的数据
            dataToA->status++;    // 设置状态为A进程传来的id值
            dataToA->result += 1.0;             // 结果值累加
            printf("Send To A: status = %d, result = %f\n", dataToA->status, dataToA->result);
            sem_post(sem_write_to_a);
        } else {
            // 处理信号量不可用的情况
            printf("Waiting for A process write data\n");
        }
        #endif

        #if 0  // 信号量为非阻塞式的  读失败，写的数据也在更新；会导致信号量增加的很大，另一个进程退出，还得读一段时间
        // 写BToA的数据
        dataToA->status++;    // 状态设置为A进程传来的id值
        dataToA->result += 1.0;             // 结果值累加
        printf("Send To A: status = %d, result = %f\n", dataToA->status, dataToA->result);
        sem_post(sem_write_to_a);  // 通知A数据已准备好

        // 尝试从AToB读取数据
        if (sem_trywait(sem_write_to_b) == 0) {
            // 成功获取信号量，读取数据
            printf("Received from A: id = %d, value = %f\n", dataFromA->id, dataFromA->value);
        } else {
            // 信号量不可用，处理非阻塞失败情况
            printf("Data from A is not ready yet\n");
            // 可以选择稍后重试或执行其他操作
        }
        #endif

        nanosleep(&ts, NULL); // 暂停以模拟处理时间
    }

    return 0;
}

