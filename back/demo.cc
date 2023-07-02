#include"Reactor/Reactor.hpp"
#include"HTTP/HTTP.hpp"
#include"MySqlPool/MySqlPool.hpp"

//采用Tencent第三方库rapidjson
#include"rapidjson/rapidjson.h"
#include"rapidjson/document.h"
#include"rapidjson/writer.h"
using namespace rapidjson;

void Accept_cb(Reactor R , Server_Ptr server)
{
    int clientfd = server->Accept();
    R.Add_Reactor(clientfd,EPOLLIN);
}


void Read_cb(Reactor R ,Server_Ptr server,MySqlPool& pool,HTTP& http)
{
    Client_Ptr client = server->Get_Client(R.Get_Now_Event().data.fd);
    server->Set_Buffer_Size(2048);
    //std::cout<<R.Get_Now_Event().data.fd<<std::endl;
    if(server->Recv(R.Get_Now_Event().data.fd)==0)
    {
        R.Del_Reactor(R.Get_Now_Event().data.fd,EPOLLIN);
        server->Close(R.Get_Now_Event().data.fd);
        server->Del_Client(R.Get_Now_Event().data.fd);
        return ;
    }
    string buffer = client->Get_Rbuffer();
    //std::cout<<buffer<<std::endl;
    http.Request_Decode(buffer);
    if(http.Request_Get_Http_Type() =="OPTIONS")
    {
        http.Response_Set_Status(200);
    }else if(http.Request_Get_Http_Type() == "POST"){
        //处理POST登录请求
        Http_String body(http.Request_Get_Body());
        Document d;
        d.Parse(body.cbegin());
        string password=d["password"].GetString();
        string username=d["username"].GetString();
        MySqlConn* conn=pool.Get_Conn(-1);
        //std::cout<<password<<std::endl;
        string condition="username = '"+username+"' and password = '"+password+"'";
        string query = conn->Select_Query(MySql::Arg_List("*"),"user",condition);
        //std::cout<<query<<std::endl;
        vector<vector<string>> res = conn->Select(query);
        if(res.size() > 1){
            http.Response_Set_Status(200);
        }else{
            http.Response_Set_Status(404);
        }
    }
    http.Response_Set_Key_Value("Access-Control-Allow-Origin","*");
    http.Response_Set_Key_Value("Access-Control-Allow-Methods","POST, GET, OPTIONS");
    http.Response_Set_Key_Value("Access-Control-Allow-Headers","Content-Type, Authorization");
    string response = http.Response_Content_Head();
    //std::cout<<response<<std::endl;
    client->Set_Wbuffer(response);
    client->Clean_Rbuffer();
    R.Mod_Reactor(R.Get_Now_Event().data.fd,EPOLLOUT);
}

void Write_cb(Reactor R,Server_Ptr server,HTTP &http)
{
    server->Send(R.Get_Now_Event().data.fd);
    Client_Ptr client=server->Get_Client(R.Get_Now_Event().data.fd);
    if(http.Request_Get_Http_Type() == "POST")
    {
        client->Set_Wbuffer("\r\n\r\n");
        server->Send(R.Get_Now_Event().data.fd);
        R.Del_Reactor(R.Get_Now_Event().data.fd,EPOLLIN);
        server->Close(R.Get_Now_Event().data.fd);
        server->Del_Client(R.Get_Now_Event().data.fd);
    }
    R.Mod_Reactor(R.Get_Now_Event().data.fd,EPOLLIN);
}

int main()
{
    Reactor R;
    Server_Ptr server=R.Get_Server();
    MySqlPool pool("pool","127.0.0.1","test","gong","123456",3306);
    HTTP http;//htpp解析类
    server->Init_Sock(9999,10);
    R.Add_Reactor(server->Get_Sock(),EPOLLIN);
    //设置非阻塞
    R.Set_No_Block(server->Get_Sock());
    R.Accept_cb=bind(Accept_cb,R,server);
    R.Read_cb=bind(Read_cb,R,server,std::ref(pool),std::ref(http));
    R.Write_cb=bind(Write_cb,R,server,std::ref(http));
    R.Deal_Event();
    return 0;
}