#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>

typedef struct {
    int id;
    double value;
} DataFromA;

typedef struct {
    int id;
    float result;
    char status;
} DataFromB;

#define KEY_A_TO_B ftok("shmfileA", 65)  // 使用文件和数字生成独特的键值
#define KEY_B_TO_A ftok("shmfileB", 66)
#define KEY_SEM ftok("semfile", 67)

int main() {
    // 为AToB创建共享内存，只写权限
    int shmIdAToB = shmget(KEY_A_TO_B, sizeof(DataFromA), 0666|IPC_CREAT);
    DataFromA *dataAToB = (DataFromA *)shmat(shmIdAToB, NULL, 0);

    // 为BToA访问共享内存，只读权限
    int shmIdBToA = shmget(KEY_B_TO_A, sizeof(DataFromB), 0666);
    DataFromB *dataBToA = (DataFromB *)shmat(shmIdBToA, NULL, SHM_RDONLY);

    // 创建和访问信号量
    int semId = semget(KEY_SEM, 2, 0666);

    while (1) {
        // 写入数据到AToB
        dataAToB->id++;
        dataAToB->value += 100.0;

        // 通知B数据已写入
        struct sembuf op = {0, 1, 0}; // sem_num为0, sem_op为1 (V操作), sem_flg为0
        semop(semId, &op, 1);

        // 等待B的数据处理
        op.sem_num = 1; // sem_num为1 (用于读取BToA的信号量)
        op.sem_op = -1; // sem_op为-1 (P操作)
        semop(semId, &op, 1);

        // 读取B的数据
        printf("Process A read: %d, %f, %c\n", dataBToA->id, dataBToA->result, dataBToA->status);

        usleep(7000); // 7ms延时
    }

    shmdt(dataAToB);  // 断开共享内存映射
    shmdt(dataBToA);
    return 0;
}

