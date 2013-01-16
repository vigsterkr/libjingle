/*
 * libjingle
 * Copyright 2013, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


package org.webrtc;

import java.util.LinkedList;
import java.util.List;

/**
 * Java-land version of the PeerConnection APIs; wraps the C++ API
 * http://www.webrtc.org/reference/native-apis, which in turn is inspired by the
 * JS APIs: http://dev.w3.org/2011/webrtc/editor/webrtc.html and
 * http://www.w3.org/TR/mediacapture-streams/
 */
public class PeerConnection {
  static {
    System.loadLibrary("jingle_peerconnection_so");
  }

  /** Java version of PeerConnectionObserver. */
  public static interface Observer {
    /** Tracks PeerConnectionObserver.StateType */
    enum StateType {
      SIGNALING, ICE
    }

    /** Triggered when a new ICE candidate has been found. */
    public void onIceCandidate(IceCandidate candidate);

    /** Triggered on any error. */
    public void onError();

    /** Triggered when SignalingState or IceState have changed. */
    public void onStateChange(StateType stateChanged);

    /** Triggered when media is received on a new stream from remote peer. */
    public void onAddStream(MediaStream stream);

    /** Triggered when a remote peer close a stream. */
    public void onRemoveStream(MediaStream stream);
  }

  /** Java version of JsepInterface.IceServer. */
  public static class IceServer {
    public final String uri;
    public final String password;

    public IceServer(String uri, String password) {
      this.uri = uri;
      this.password = password;
    }
  }

  private final List<MediaStream> localStreams;
  private final long nativePeerConnection;
  private final long nativeObserver;

  PeerConnection(long nativePeerConnection, long nativeObserver) {
    this.nativePeerConnection = nativePeerConnection;
    this.nativeObserver = nativeObserver;
    localStreams = new LinkedList<MediaStream>();
  }

  // JsepInterface.
  public native SessionDescription getLocalDescription();

  public native SessionDescription getRemoteDescription();

  public native void createOffer(
      SdpObserver observer, MediaConstraints constraints);

  public native void createAnswer(
      SdpObserver observer, MediaConstraints constraints);

  public native void setLocalDescription(
      SdpObserver observer, SessionDescription sdp);

  public native void setRemoteDescription(
      SdpObserver observer, SessionDescription sdp);

  public native boolean updateIce(
      List<IceServer> iceServers, MediaConstraints constraints);

  public boolean addIceCandidate(IceCandidate candidate) {
    return nativeAddIceCandidate(
        candidate.sdpMid, candidate.sdpMLineIndex, candidate.sdp);
  }

  // PeerConnectionInterface.
  /** Tracks PeerConnectionInterface.SignalingState */
  public enum SignalingState {
    STABLE, HAVE_LOCAL_OFFER, HAVE_LOCAL_PRANSWER, HAVE_REMOTE_OFFER,
    HAVE_REMOTE_PRANSWER, CLOSED,
  }

  /** Tracks PeerConnectionInterface.IceState */
  public enum IceState {
    ICE_NEW, ICE_GATHERING, ICE_WAITING, ICE_CHECKING, ICE_CONNECTED,
    ICE_COMPLETED, ICE_FAILED, ICE_CLOSED,
  }

  public boolean addStream(
      LocalMediaStream stream, MediaConstraints constraints) {
    boolean ret = nativeAddLocalStream(stream.nativeStream, constraints);
    if (!ret) {
      return false;
    }
    localStreams.add(stream);
    return true;
  }

  public void removeStream(LocalMediaStream stream) {
    nativeRemoveLocalStream(stream.nativeStream);
    localStreams.remove(stream);
  }

  // TODO(fischman): add support for DTMF-related methods once that API
  // stabilizes.
  public native SignalingState signalingState();

  public native IceState iceState();

  public void dispose() {
    for (MediaStream stream : localStreams) {
      stream.dispose();
    }
    localStreams.clear();
    freePeerConnection(nativePeerConnection);
    freeObserver(nativeObserver);
  }

  private static native void freePeerConnection(long nativePeerConnection);

  private static native void freeObserver(long nativeObserver);

  private native boolean nativeAddIceCandidate(
      String sdpMid, int sdpMLineIndex, String iceCandidateSdp);

  private native boolean nativeAddLocalStream(
      long nativeStream, MediaConstraints constraints);

  private native void nativeRemoveLocalStream(long nativeStream);
}
