/*
 * Copyright (C) 2002-2003 Fhg Fokus
 *
 * This file is part of SEMS, a free SIP media server.
 *
 * SEMS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. This program is released under
 * the GPL with the additional exemption that compiling, linking,
 * and/or using OpenSSL is allowed.
 *
 * For a license to use the SEMS software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact iptel.org by e-mail at the following addresses:
 *    info@iptel.org
 *
 * SEMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "AmRtpReceiver.h"
#include "AmRtpStream.h"
#include "AmRtpPacket.h"
#include "log.h"
#include "AmConfig.h"

#include <errno.h>

// Not on Solaris!
#if !defined (__SVR4) && !defined (__sun)
#include <strings.h>
#endif

#include <sys/time.h>
#ifdef USE_EPOLL
#include <sys/epoll.h>
#else
#include <sys/poll.h>
#endif

#ifndef MAX_RTP_SESSIONS
#define MAX_RTP_SESSIONS 2048
#endif 

#ifdef USE_EPOLL
#define RTP_POLL_TIMEOUT 500 /*500 ms*/
#else
#define RTP_POLL_TIMEOUT 50 /*50 ms*/
#endif


_AmRtpReceiver::_AmRtpReceiver()
{
  n_receivers = AmConfig::RTPReceiverThreads;
  receivers = new AmRtpReceiverThread[n_receivers];
}

_AmRtpReceiver::~_AmRtpReceiver()
{
  delete [] receivers;
}

AmRtpReceiverThread::AmRtpReceiverThread()
  : stop_requested(false)
#ifdef USE_EPOLL
  , poll_fd(-1)
#endif
{
#ifndef USE_EPOLL
  fds  = new struct pollfd[MAX_RTP_SESSIONS];
#endif
  nfds = 0;
}

AmRtpReceiverThread::~AmRtpReceiverThread()
{
#ifndef USE_EPOLL
  delete [] (fds);
#endif
  INFO("RTP receiver has been recycled.\n");
}

void AmRtpReceiverThread::on_stop()
{
  INFO("requesting RTP receiver to stop.\n");
  stop_requested.set(true);
}

void AmRtpReceiverThread::stop_and_wait()
{
  if(!is_stopped()) {
    stop();
    
    while(!is_stopped()) 
      usleep(10000);
  }
}

void _AmRtpReceiver::dispose() 
{
  for(unsigned int i=0; i<n_receivers; i++){
    receivers[i].stop_and_wait();
  }
}

void AmRtpReceiverThread::run()
{

#ifdef USE_EPOLL
  struct epoll_event events[MAX_RTP_SESSIONS];

  poll_fd = epoll_create(MAX_RTP_SESSIONS);
    if (poll_fd == -1) {
      throw string("failed epoll_create in AmRtpReceiverThread: "+string(strerror(errno)));
    }
#else
  unsigned int   tmp_nfds = 0;
  struct pollfd* tmp_fds  = new struct pollfd[MAX_RTP_SESSIONS];
#endif

  setThreadName("rtp-rx");
  while(!stop_requested.get()){
#ifdef USE_EPOLL
    int ret = epoll_wait(poll_fd,events,MAX_RTP_SESSIONS,RTP_POLL_TIMEOUT);
    if(ret == -1 && errno != EINTR){
      ERROR("AmRtpReceiver: epoll_wait: %s\n",strerror(errno));
    }
    if(ret < 1)
      continue;

    streams_mut.lock();
    for (int n = 0; n < ret; ++n) {
      struct epoll_event &e = events[n];
      if(!(e.events & EPOLLIN)){
        continue;
      }
      StreamInfo *info = reinterpret_cast<StreamInfo *>(e.data.ptr);
      AmRtpStream *stream = info->stream;
      if(!stream){
        //this descriptor scheduled for removal
        delete info;
        continue;
      }
      stream->recvPacket(info->fd);
    }
    streams_mut.unlock();
#else
    streams_mut.lock();
    tmp_nfds = nfds;
    memcpy(tmp_fds,fds,nfds*sizeof(struct pollfd));
    streams_mut.unlock();

    int ret = poll(tmp_fds,tmp_nfds,RTP_POLL_TIMEOUT);
    if(ret < 0 && errno != EINTR)
      ERROR("AmRtpReceiver: poll: %s\n",strerror(errno));

    if(ret < 1)
      continue;

    for(unsigned int i=0; i<tmp_nfds; i++) {

      if(!(tmp_fds[i].revents & POLLIN))
        continue;

      streams_mut.lock();
      Streams::iterator it = streams.find(tmp_fds[i].fd);
      if(it != streams.end()) {
        it->second.stream->recvPacket(tmp_fds[i].fd);
      }
      streams_mut.unlock();
    }
#endif //#ifdef USE_EPOLL

  } //while(!stop_requested.get())

#ifdef USE_EPOLL
  close(poll_fd);
#else
  delete[] (tmp_fds);
#endif

}

void AmRtpReceiverThread::addStream(int sd, AmRtpStream* stream)
{

  streams_mut.lock();

  if(streams.find(sd) != streams.end()) {
    ERROR("trying to insert existing stream [%p] with sd=%i\n",
    stream,sd);
    streams_mut.unlock();
    return;
  }

  if(nfds >= MAX_RTP_SESSIONS){
    streams_mut.unlock();
    ERROR("maximum number of sessions reached (%i)\n",
    MAX_RTP_SESSIONS);
    throw string("maximum number of sessions reached");
  }

#ifdef USE_EPOLL
  struct epoll_event ev;

  std::pair<Streams::iterator, bool> s_it = streams.insert(std::make_pair(sd,new StreamInfo(sd,stream)));

  ev.events = EPOLLIN;
  ev.data.ptr = s_it.first->second;

  if(epoll_ctl(poll_fd,EPOLL_CTL_ADD,sd,&ev)==-1){
    delete s_it.first->second;
    streams.erase(s_it.first);
    streams_mut.unlock();
    ERROR("failed to add to epoll structure stream [%p] with sd=%i, error: %s\n",
        stream,sd,strerror(errno));
    return;
  }
#else
  fds[nfds].fd      = sd;
  fds[nfds].events  = POLLIN;
  fds[nfds].revents = 0;

  streams.insert(std::make_pair(sd,StreamInfo(nfds,stream)));
#endif

  nfds++;

  streams_mut.unlock();
}

void AmRtpReceiverThread::removeStream(int sd)
{
  streams_mut.lock();

  Streams::iterator sit = streams.find(sd);
  if(sit == streams.end()) {
    streams_mut.unlock();
    return;
  }

#ifdef USE_EPOLL
  if(epoll_ctl(poll_fd,EPOLL_CTL_DEL,sd,NULL)==-1){
      ERROR("removeStream epoll_ctl_del stream [%p] with sd = %i error %s",
            sit->second->stream, sd, strerror(errno));
  }
  sit->second->stream = NULL; //schedule for removal
  streams.erase(sit);
  --nfds;
#else
  unsigned int i = sit->second.index;
  if(--nfds && (i < nfds)) {
    fds[i] = fds[nfds];
    sit = streams.find(fds[nfds].fd);
    if(sit != streams.end()) {
      sit->second.index = i;
    }
  }
  streams.erase(sd);

#endif
  streams_mut.unlock();
}

void _AmRtpReceiver::start()
{
  for(unsigned int i=0; i<n_receivers; i++)
    receivers[i].start();
}

void _AmRtpReceiver::addStream(int sd, AmRtpStream* stream)
{
  unsigned int i = sd % n_receivers;
  receivers[i].addStream(sd,stream);
}

void _AmRtpReceiver::removeStream(int sd)
{
  unsigned int i = sd % n_receivers;
  receivers[i].removeStream(sd);
}
