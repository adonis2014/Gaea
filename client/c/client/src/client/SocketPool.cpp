/*
 *  Copyright Beijing 58 Information Technology Co.,Ltd.
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an
 *  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 *  specific language governing permissions and limitations
 *  under the License.
 */
/*
 * SocketPool.cpp
   *
 * Created on: 2011-7-5
 * Author: Service Platform Architecture Team (spat@58.com)
 */

#include "SocketPool.h"
#include "CSocket.h"
#include "SocketPoolProfile.h"
#include "DataReceiver.h"
#include <pthread.h>
#include <unistd.h>
#include <stdexcept>
namespace gaea {
SocketPool::SocketPool(char *hostName, int port, SocketPoolProfile *config) {
	csocketQueue = new std::list<int>;
	pthread_mutex_init(&queueMutex, NULL);
	this->hostName = hostName;
	this->port = port;
	this->config = config;
	this->maxPoolSize = config->getMaxPoolSize();
	minPoolSize = config->getMinPoolSize();
	minFreeCount = 0;
	socketCount = 0;
	lastCheckTime = time(NULL);
	shrinkInterval = config->getShrinkInterval();
}
int SocketPool::getSocket() {
	int fd = 0;
	pthread_mutex_lock(&queueMutex);
	if (csocketQueue->size() > 0) {
		fd = queuePop();
	} else if (socketCount < maxPoolSize) {
		fd = CSocket::createSocket(hostName, port, this, config);
		if (fd > 0) {
			++socketCount;
			DataReceiver::GetInstance()->registerSocket(fd, this);
		} else {
			fd = -1;
			gaeaLog(GAEA_WARNING, "create socket failed. hostName:%s\n", hostName);
		}
	} else {
		if (csocketQueue->size() > 0) {
			fd = queuePop();
		} else {
			errno = -5;
			gaeaLog(GAEA_WARNING, "socket pool size=%d\n", 0);
		}
	}
	if (minFreeCount > csocketQueue->size()) {
		minFreeCount = csocketQueue->size();
	}
	pthread_mutex_unlock(&queueMutex);
	if (fd <= 0) {
		errno = -5;
	}
	return fd;
}
void SocketPool::releaseSocket(int fd) {
	if (fd > 0) {
		pthread_mutex_lock(&queueMutex);
		csocketQueue->push_back(fd);
		pthread_mutex_unlock(&queueMutex);
		if (shrinkInterval > 0 && lastCheckTime + shrinkInterval < time(NULL)) {
			pthread_mutex_lock(&queueMutex);
			if (shrinkInterval > 0 && lastCheckTime + shrinkInterval < time(NULL)) {
				lastCheckTime = time(NULL);
				int fd = 0;
				int count = socketCount;
				for (size_t i = 0; i < minFreeCount; ++i) {
					if (count - i < minPoolSize || csocketQueue->size() == 0) {
						break;
					}
					fd = csocketQueue->front();
					csocketQueue->pop_front();
					--socketCount;
					CSocket::closeSocket(fd);
				}
				minFreeCount = socketCount;
			}
			pthread_mutex_unlock(&queueMutex);
		}
	}
}
void SocketPool::closeSocket(int fd) {
	if (fd > 0) {
		pthread_mutex_lock(&queueMutex);
		csocketQueue->remove(fd);
		--socketCount;
		pthread_mutex_unlock(&queueMutex);
		DataReceiver::GetInstance()->unRegisterSocket(fd);
		CSocket::closeSocket(fd);
	}
}
int SocketPool::queuePop() {
	int fd = 0;
	if (csocketQueue->size() > 0) {
		fd = csocketQueue->back();
		csocketQueue->pop_back();
	}
	return fd;
}
SocketPool::~SocketPool() {
	delete csocketQueue;
	pthread_mutex_destroy(&queueMutex);
}
void SocketPool::closeAllSocket() {
	pthread_mutex_lock(&queueMutex);
	int fd;
	while (csocketQueue->size() > 0) {
		fd = csocketQueue->back();
		csocketQueue->pop_back();
		DataReceiver::GetInstance()->unRegisterSocket(fd);
		CSocket::closeSocket(fd);
	}
	socketCount = 0;
	pthread_mutex_unlock(&queueMutex);
}
}
