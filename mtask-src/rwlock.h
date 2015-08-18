//
//  rwlock.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__rwlock__
#define __mtask__rwlock__


struct rwlock {
    int write;
    int read;
};


static inline void
rwlock_init(struct rwlock *lock) {
    lock->write = 0;
    lock->read = 0;
}

static inline void
rwlock_rlock(struct rwlock *lock) {
    for (;;) {
        /*等待 写操作 释放*/
        /*这里只是一个优化*/
        while(lock->write) {
            /*内存屏蔽*/
            __sync_synchronize();
        }
        /*尝试获取读标志*/
        __sync_add_and_fetch(&lock->read,1);
        /*检查是否有 写 用户*/
        if (lock->write) {
            /*读锁获取失败,还原 读计数*/
            __sync_sub_and_fetch(&lock->read,1);
        } else {
            /*读锁获取成功,跳出阻塞*/
            break;
        }
    }
}

static inline void
rwlock_wlock(struct rwlock *lock) {
    /**//*原子设置写标志*/
    while (__sync_lock_test_and_set(&lock->write,1)) {}
    /*等待所有读方退出读锁持有区域*/
    while(lock->read) {
        /*内存障壁*/
        __sync_synchronize();
    }
}

static inline void
rwlock_wunlock(struct rwlock *lock) {
    /*将写标志设为0*/
    __sync_lock_release(&lock->write);
}


static inline void
rwlock_runlock(struct rwlock *lock) {
    /*将读标志原子减1*/
    __sync_sub_and_fetch(&lock->read,1);
}




#endif /* defined(__mtask__rwlock__) */
