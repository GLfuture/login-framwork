#pragma once
#ifndef MYSQLPOOL_H
#define MYSQLPOOL_H
#endif
#include "MySql.hpp"
#include <condition_variable>
#include <list>
#define DEFAULT_MAX_CONN 8
#define DEFAULT_MIN_CONN 4
#define log_info(x) std::cout << x << std::endl
class MySqlPool;

class MySqlConn : public MySql
{
public:
    /// @brief
    MySqlConn(MySqlPool *ptr) : pool(ptr) {}
    MySqlConn(MySqlConn *mysql_conn) = delete;

private:
    MySqlPool *pool;
};

class MySqlPool
{
public:
    MySqlPool() {}

    MySqlPool(const MySqlPool &pool)
    {
        this->conn_list = pool.conn_list;
        this->db_cur_conn_cnt = pool.db_cur_conn_cnt;
        this->db_max_conn_cnt = pool.db_max_conn_cnt;
        this->DB_Name = pool.DB_Name;
        this->Is_terminate = pool.Is_terminate;
        this->Password = pool.Password;
        this->Pool_Name = pool.Pool_Name;
        this->User_Name = pool.User_Name;
        this->wait_cnt = pool.wait_cnt;
        this->_port = pool._port;
        this->_remote = pool._remote;
    }

    MySqlPool(string pool_name, string host, string db_name, string user_name, string password, uint32_t port, uint32_t min_conn = DEFAULT_MIN_CONN, uint32_t max_conn = DEFAULT_MAX_CONN)
        : Pool_Name(pool_name), _remote(host), DB_Name(db_name), User_Name(user_name), Password(password), db_cur_conn_cnt(min_conn), db_max_conn_cnt(max_conn),
          _port(port), Is_terminate(false), wait_cnt(0)
    {
        uint16_t ret = Init_Conn();
        if (ret != 0)
            throw ret;
    }

    // 获取连接
    MySqlConn *Get_Conn(const int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(this->mtx);
        if (Is_terminate)
        {
            log_err("Pool is terminate");
            return NULL;
        }
        if (conn_list.empty())
        {
            if (db_cur_conn_cnt >= db_max_conn_cnt) // 达到最大连接数
            {
                if (timeout_ms <= 0) // 死等
                {
                    log_info("wait ms:" + timeout_ms);
                    cond.wait(lock, [this]()
                              {
                        //log_info("wait:"<<(wait_cnt++));
                        return (!conn_list.empty()) | Is_terminate; });
                }
                else
                { // 超时等待
                    cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]()
                                  {
                        //log_info("wait_for:"<<(wait_cnt++));
                        return (!conn_list.empty()) | Is_terminate; });

                    if (conn_list.empty()) // 超时队列为空则退出
                        return NULL;
                }
                if (Is_terminate)
                {
                    log_info("Pool is terminate");
                }
            }
            else // 还没达到最大连接数
            {
                MySqlConn *new_conn = new MySqlConn(this);
                int ret = new_conn->Connect(this->_remote, this->User_Name, this->Password, this->DB_Name, this->_port);
                if (ret != 0)
                {
                    delete new_conn;
                    log_err("new conn failed");
                    return NULL;
                }
                conn_list.push_back(new_conn);
                db_cur_conn_cnt++;
            }
        }
        MySqlConn *conn_ptr = conn_list.front();
        conn_list.pop_front();
        lock.unlock();
        return conn_ptr;
    }
    // 归还连接
    int32_t Ret_Conn(MySqlConn *mysql_conn)
    {
        std::list<MySqlConn *>::iterator it = conn_list.begin();
        for (; it != conn_list.end(); it++)
        {
            if (*it == mysql_conn)
            {
                log_err("connection return reapted");
                return ERROR;
            }
        }
        std::unique_lock<std::mutex> lock(this->mtx);
        conn_list.push_back(mysql_conn);
        cond.notify_one();
        return 0;
    }
    // 销毁连接池
    void Destory()
    {
        int n = conn_list.size();
        for (int i = 0; i < n; i++)
        {
            MySqlConn *conn = conn_list.front();
            conn_list.pop_front();
            delete conn;
        }
    }

    // 获取连接池的姓名
    const char *Get_Pool_Name() { return Pool_Name.c_str(); }
    const char *Get_DBServer_IP() { return _remote.c_str(); }
    uint32_t Get_DBServer_Port() { return _port; }
    const char *Get_Username() { return User_Name.c_str(); }
    const char *Get_Passwrod() { return Password.c_str(); }
    const char *Get_DBName() { return DB_Name.c_str(); }

private:
    // 初始化连接
    uint32_t Init_Conn()
    {
        for (int i = 0; i < DEFAULT_MIN_CONN; i++)
        {
            MySqlConn *conn_ptr = new MySqlConn(this);
            int ret = conn_ptr->Connect(this->_remote, this->User_Name, this->Password, this->DB_Name, this->_port);
            if (ret != 0)
            {
                delete conn_ptr;
                return ret;
            }
            conn_list.push_back(conn_ptr);
        }
        return 0;
    }

private:
    string Pool_Name; // 连接池名
    string _remote;   // 远程主机
    uint32_t _port;   // 端口
    string DB_Name;   // database
    string User_Name; // 用户名
    string Password;  // 密码

    uint32_t db_cur_conn_cnt; // 当前连接数
    uint32_t db_max_conn_cnt; // 最大连接数

    std::list<MySqlConn *> conn_list;

    std::mutex mtx;
    std::condition_variable cond;

    bool Is_terminate;
    uint32_t wait_cnt; // 正在等待的连接数
};