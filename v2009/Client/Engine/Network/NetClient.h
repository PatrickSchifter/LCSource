
#ifndef		NET_CLIENT_H_
#define		NET_CLIENT_H_

#include <boost/asio.hpp>

class CNetSocket
{
public:
	CNetSocket(boost::asio::io_context& io);
	~CNetSocket();

	bool	isValid();

	bool	connect(std::string& IP, std::string& port);
	void	send(CNetworkMessage &nmMessage);
	void	recv();

private:
	boost::asio::ip::tcp::socket	_socket;
	bool	_bConnect;
};

class CNetClient
{
public:
	CNetClient();
	~CNetClient();

	bool init(std::string& ip, std::string& port);
	void send(CNetworkMessage& nmMessage);
	void recv();

	boost::asio::io_context& get_service()		{ return _io_service; }

private:
	boost::asio::io_context	_io_service;
	CNetSocket* _pSock;
};

#endif		// NET_CLIENT_H_