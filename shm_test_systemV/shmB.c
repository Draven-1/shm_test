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

#define KEY_A_TO_B ftok("shmfileA", 65)
#define KEY_B_TO_A ftok("shmfileB", 66)
#define KEY_SEM ftok("semfile", 67)

int main() {
    // 为AToB访问共享内存，只读权限
    int shmIdAToB = shmget(KEY_A_TO_B, sizeof(DataFromA), 0666);
    DataFromA *dataAToB = (DataFromA *)shmat(shmIdAToB, NULL, SHM_RDONLY);

    // 为BToA创建共享内存，只写权限
    int shmIdBToA = shmget(KEY_B_TO_A, sizeof(DataFromB), 0666|IPC_CREAT);
    DataFromB *dataBToA = (DataFromB *)shmat(shmIdBToA, NULL, 0);

    // 创建和访问信号量
    int semId = semget(KEY_SEM, 2, 0666|IPC_CREAT);

    while (1) {
        // 等待A的数据写入
        struct sembuf op = {0, -1, 0}; // sem_num为0, sem_op为-1 (P操作), sem_flg为0
        semop(semId, &op, 1);

        // 读取A的数据
        printf("Process B reads: %d, %f\n", dataAToB->id, dataAToB->value);

        // 写入数据到BToA
        dataBToA->id = dataAToB->id + 1;
        dataBToA->result = dataAToB->value + 1.0;
        dataBToA->status = 'B';

        // 通知A数据已写入
        op.sem_num = 1; // sem_num为1 (用于写入BToA的信号量)
        op.sem_op = 1;  // sem_op为1 (V操作)
        semop(semId, &op, 1);

        usleep(7000); // 7ms延时
    }

    shmdt(dataAToB);  // 断开共享内存映射
    shmdt(dataBToA);
    return 0;
}

