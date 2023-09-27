#ifndef WEBRTC_MANAGER_H_
#define WEBRTC_MANAGER_H_

#include "VideoSource.h"
#include <mutex>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "peer_connection_client.h"

namespace webrtc {
class VideoCaptureModule;
}  // namespace webrtc

namespace cricket {
class VideoRenderer;
}  // namespace cricket

template <typename T>
class AtomicQueue {
 public:
  void push(const T& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queque.push(value);
  }

  void pop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queque.pop();
  }

  T front() { 
      return m_queque.front();
  }

  int size() { 
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queque.size();
  }

 private:
  std::queue<T> m_queque;
  mutable std::mutex m_mutex;
};

enum EWebRTCTasks {
    eLOGIN,
    eCONNECT,
    eSEND_MESSAGE_TO_PEER,
    eDISCONNECT
};


class WebRTCManager : public webrtc::PeerConnectionObserver,
                  public webrtc::CreateSessionDescriptionObserver,
                  public PeerConnectionClientObserver {
 public:

  std::shared_ptr<AtomicQueue< std::pair<EWebRTCTasks,void*> >> task_queue;

  bool MainLoop();

  enum CallbackID {
    MEDIA_CHANNELS_INITIALIZED = 1,
    PEER_CONNECTION_CLOSED,
    SEND_MESSAGE_TO_PEER,
    NEW_TRACK_ADDED,
    TRACK_REMOVED,
  };

  WebRTCManager(PeerConnectionClient* client, CustomVideoSource* customVideoSource, std::shared_ptr<AtomicQueue< std::pair<EWebRTCTasks, void*> >> taskQueue);

  bool connection_active() const;

  void Close();

 protected:
  ~WebRTCManager();
  bool InitializePeerConnection();
  bool ReinitializePeerConnectionForLoopback();
  bool CreatePeerConnection(bool dtls);
  void DeletePeerConnection();
  void EnsureStreamingUI();
  void AddTracks();

  //
  // PeerConnectionObserver implementation.
  //

  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnAddTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
          streams) override;
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override {}
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}

  //
  // PeerConnectionClientObserver implementation.
  //

  void OnSignedIn() override;

  void OnDisconnected() override;

  void OnPeerConnected(int id, const std::string& name) override;

  void OnPeerDisconnected(int id) override;

  void OnMessageFromPeer(int peer_id, const std::string& message) override;

  void OnMessageSent(int err) override;

  void OnServerConnectionFailure() override;

  //
  // MainWndCallback implementation.
  //

  void StartLogin(const std::string& server, int port);

  void DisconnectFromServer();

  void ConnectToPeer(int peer_id);

  void DisconnectFromCurrentPeer();

  // CreateSessionDescriptionObserver implementation.
  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  void OnFailure(webrtc::RTCError error) override;

 protected:
  // Send a message to the remote peer.
  void SendMessage(const std::string& json_object);
  

  int peer_id_;
  bool loopback_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  CustomVideoSource* preSetVideoSource;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  PeerConnectionClient* client_;
  std::deque<std::string*> pending_messages_;
  std::string m_server;
  int m_port;
  int peer_id = -1;
};

#endif  // WEBRTC_MANAGER_H_
