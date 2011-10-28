/*
 * libjingle
 * Copyright 2004--2011, Google Inc.
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

#include "talk/session/phone/mediamessages.h"

#include <string>
#include <vector>

#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/p2p/base/constants.h"
#include "talk/session/phone/mediasessionclient.h"
#include "talk/xmllite/xmlelement.h"

// Unit tests for mediamessages.cc.

namespace cricket {

namespace {

static const char kViewVideoNoneXml[] =
    "<view xmlns='google:jingle'"
    "  name='video1'"
    "  type='none'"
    "/>";

static const char kNotifyEmptyXml[] =
    "<notify xmlns='google:jingle'"
    "  name='video1'"
    "/>";

class MediaMessagesTest : public testing::Test {
 public:
  // CreateMediaSessionDescription uses a static variable cricket::NS_JINGLE_RTP
  // defined in another file and cannot be used to initialize another static
  // variable (http://www.parashift.com/c++-faq-lite/ctors.html#faq-10.14)
  MediaMessagesTest()
      : remote_description_(CreateMediaSessionDescription("audio1", "video1")) {
  }

 protected:
  static std::string ViewVideoStaticVgaXml(const std::string& ssrc) {
      return "<view xmlns='google:jingle'"
             "  name='video1'"
             "  type='static'"
             "  ssrc='" + ssrc + "'"
             ">"
             "<params"
             "    width='640'"
             "    height='480'"
             "    framerate='30'"
             "    preference='0'"
             "  />"
             "</view>";
  }

  static std::string NotifyAddXml(const std::string& content_name,
                                  const std::string& nick,
                                  const std::string& name,
                                  const std::string& usage,
                                  const std::string& ssrc) {
    return "<notify xmlns='google:jingle'"
           "  name='" + content_name + "'"
           ">"
           "  <source"
           "    nick='" + nick + "'"
           "    name='" + name + "'"
           "    usage='" + usage + "'"
           "  >"
           "    <ssrc>" + ssrc + "</ssrc>"
           "  </source>"
           "</notify>";
  }

  static std::string NotifyTwoSourceXml(const std::string& name,
                                        const std::string& nick1,
                                        const std::string& ssrc1,
                                        const std::string& nick2,
                                        const std::string& ssrc2) {
    return "<notify xmlns='google:jingle'"
           "  name='" + name + "'"
           ">"
           "  <source"
           "    nick='" + nick1 + "'"
           "  >"
           "    <ssrc>" + ssrc1 + "</ssrc>"
           "  </source>"
           "  <source"
           "    nick='" + nick2 + "'"
           "  >"
           "    <ssrc>" + ssrc2 + "</ssrc>"
           "  </source>"
           "</notify>";
  }

  static std::string NotifyImplicitRemoveXml(const std::string& content_name,
                                             const std::string& nick) {
    return "<notify xmlns='google:jingle'"
           "  name='" + content_name + "'"
           ">"
           "  <source"
           "    nick='" + nick + "'"
           "  >"
           "  </source>"
           "</notify>";
  }

  static std::string NotifyExplicitRemoveXml(const std::string& content_name,
                                             const std::string& nick,
                                             const std::string& ssrc) {
    return "<notify xmlns='google:jingle'"
           "  name='" + content_name + "'"
           ">"
           "  <source"
           "    nick='" + nick + "'"
           "    state='removed'"
           "  >"
           "    <ssrc>" + ssrc + "</ssrc>"
           "  </source>"
           "</notify>";
  }

  static cricket::SessionDescription* CreateMediaSessionDescription(
      const std::string& audio_content_name,
      const std::string& video_content_name) {
    cricket::SessionDescription* desc = new cricket::SessionDescription();
    desc->AddContent(audio_content_name, cricket::NS_JINGLE_RTP,
                     new cricket::AudioContentDescription());
    desc->AddContent(video_content_name, cricket::NS_JINGLE_RTP,
                     new cricket::VideoContentDescription());
    return desc;
  }

  talk_base::scoped_ptr<cricket::SessionDescription> remote_description_;
};

}  // anonymous namespace

// Test serializing/deserializing an empty <view> message.
TEST_F(MediaMessagesTest, ViewNoneToXml) {
  talk_base::scoped_ptr<buzz::XmlElement> expected_view_elem(
      buzz::XmlElement::ForStr(kViewVideoNoneXml));

  cricket::ViewRequest view_request;
  cricket::XmlElements actual_view_elems;
  cricket::WriteError error;

  ASSERT_TRUE(cricket::WriteViewRequest(
      "video1", view_request, &actual_view_elems, &error));

  ASSERT_EQ(1U, actual_view_elems.size());
  EXPECT_EQ(expected_view_elem->Str(), actual_view_elems[0]->Str());
}

// Test serializing/deserializing an a simple vga <view> message.
TEST_F(MediaMessagesTest, ViewVgaToXml) {
  talk_base::scoped_ptr<buzz::XmlElement> expected_view_elem1(
      buzz::XmlElement::ForStr(ViewVideoStaticVgaXml("1234")));
  talk_base::scoped_ptr<buzz::XmlElement> expected_view_elem2(
      buzz::XmlElement::ForStr(ViewVideoStaticVgaXml("2468")));

  cricket::ViewRequest view_request;
  cricket::XmlElements actual_view_elems;
  cricket::WriteError error;

  view_request.static_video_views.push_back(
      cricket::StaticVideoView(1234, 640, 480, 30));
  view_request.static_video_views.push_back(
      cricket::StaticVideoView(2468, 640, 480, 30));

  ASSERT_TRUE(cricket::WriteViewRequest(
      "video1", view_request, &actual_view_elems, &error));

  ASSERT_EQ(2U, actual_view_elems.size());
  EXPECT_EQ(expected_view_elem1->Str(), actual_view_elems[0]->Str());
  EXPECT_EQ(expected_view_elem2->Str(), actual_view_elems[1]->Str());
}

// Test serializing/deserializing an empty session-info message.
TEST_F(MediaMessagesTest, NotifyFromEmptyXml) {
  talk_base::scoped_ptr<buzz::XmlElement> action_elem(
      new buzz::XmlElement(cricket::QN_JINGLE));
  EXPECT_FALSE(cricket::IsSourcesNotify(action_elem.get()));
}

// Test serializing/deserializing an empty <notify> message.
TEST_F(MediaMessagesTest, NotifyEmptyFromXml) {
  talk_base::scoped_ptr<buzz::XmlElement> action_elem(
      new buzz::XmlElement(cricket::QN_JINGLE));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(kNotifyEmptyXml));

  cricket::MediaSources sources;
  cricket::ParseError error;

  EXPECT_TRUE(cricket::IsSourcesNotify(action_elem.get()));
  ASSERT_TRUE(cricket::ParseSourcesNotify(action_elem.get(),
                                          remote_description_.get(),
                                          &sources, &error));

  EXPECT_EQ(0U, sources.audio().size());
  EXPECT_EQ(0U, sources.video().size());
}

// Test serializing/deserializing a complex <notify> message.
TEST_F(MediaMessagesTest, NotifyFromXml) {
  talk_base::scoped_ptr<buzz::XmlElement> action_elem(
      new buzz::XmlElement(cricket::QN_JINGLE));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyAddXml(
          "video1", "Joe", "Facetime", "", "1234")));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyAddXml(
          "video1", "Bob", "Microsoft Word", "screencast", "2468")));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyAddXml(
          "video1", "Bob", "", "", "3692")));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyImplicitRemoveXml(
          "audio1", "Joe")));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyExplicitRemoveXml(
          "audio1", "Joe", "1234")));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyAddXml(
          "audio1", "Bob", "", "", "3692")));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyTwoSourceXml(
          "video1", "Joe", "1234", "Bob", "2468")));

  cricket::MediaSources sources;
  cricket::ParseError error;

  EXPECT_TRUE(cricket::IsSourcesNotify(action_elem.get()));
  ASSERT_TRUE(cricket::ParseSourcesNotify(action_elem.get(),
                                          remote_description_.get(),
                                          &sources, &error));

  ASSERT_EQ(5U, sources.video().size());
  ASSERT_EQ(3U, sources.audio().size());

  EXPECT_EQ("Joe", sources.video()[0].nick);
  EXPECT_EQ("Facetime", sources.video()[0].name);
  EXPECT_EQ("", sources.video()[0].usage);
  EXPECT_EQ(1234U, sources.video()[0].ssrc);
  EXPECT_TRUE(sources.video()[0].ssrc_set);
  EXPECT_FALSE(sources.video()[0].removed);

  EXPECT_EQ("Bob", sources.video()[1].nick);
  EXPECT_EQ("Microsoft Word", sources.video()[1].name);
  EXPECT_EQ("screencast", sources.video()[1].usage);
  EXPECT_EQ(2468U, sources.video()[1].ssrc);
  EXPECT_TRUE(sources.video()[1].ssrc_set);
  EXPECT_FALSE(sources.video()[0].removed);

  EXPECT_EQ("Bob", sources.video()[2].nick);
  EXPECT_EQ(3692U, sources.video()[2].ssrc);
  EXPECT_TRUE(sources.video()[2].ssrc_set);
  EXPECT_EQ("", sources.video()[2].name);
  EXPECT_EQ("", sources.video()[2].usage);
  EXPECT_FALSE(sources.video()[0].removed);

  EXPECT_EQ("Joe", sources.video()[3].nick);
  EXPECT_EQ(1234U, sources.video()[3].ssrc);

  EXPECT_EQ("Bob", sources.video()[4].nick);
  EXPECT_EQ(2468U, sources.video()[4].ssrc);

  EXPECT_EQ("Joe", sources.audio()[0].nick);
  EXPECT_FALSE(sources.audio()[0].ssrc_set);
  EXPECT_FALSE(sources.video()[0].removed);

  EXPECT_EQ("Joe", sources.audio()[1].nick);
  EXPECT_TRUE(sources.audio()[1].ssrc_set);
  EXPECT_EQ(1234U, sources.audio()[1].ssrc);
  EXPECT_TRUE(sources.audio()[1].removed);

  EXPECT_EQ("Bob", sources.audio()[2].nick);
  EXPECT_EQ(3692U, sources.audio()[2].ssrc);
  EXPECT_TRUE(sources.audio()[2].ssrc_set);
  EXPECT_FALSE(sources.audio()[2].removed);
}

// Test serializing/deserializing a malformed <notify> message.
TEST_F(MediaMessagesTest, NotifyFromBadXml) {
  MediaSources sources;
  ParseError error;

  // Bad ssrc
  talk_base::scoped_ptr<buzz::XmlElement> action_elem(
      new buzz::XmlElement(cricket::QN_JINGLE));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyAddXml("video1", "Joe", "", "", "XYZ")));
  EXPECT_TRUE(cricket::IsSourcesNotify(action_elem.get()));
  EXPECT_FALSE(cricket::ParseSourcesNotify(
      action_elem.get(), remote_description_.get(), &sources, &error));

  // Bad nick
  action_elem.reset(new buzz::XmlElement(cricket::QN_JINGLE));
  action_elem->AddElement(
      buzz::XmlElement::ForStr(NotifyAddXml("video1", "", "", "", "1234")));
  EXPECT_TRUE(cricket::IsSourcesNotify(action_elem.get()));
  EXPECT_FALSE(cricket::ParseSourcesNotify(
      action_elem.get(), remote_description_.get(), &sources, &error));
}

}  // namespace cricket
