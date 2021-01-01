#ifndef __UPNP_H__
#define __UPNP_H__

#include <string>

namespace UPNP
{
  class Action
  {
    public:
      int instanceId = 0;

      std::string headers() const
      {
        std::string headers = R"(Content-Type: text/xml;charset="utf-8")" "\r\n";
        headers += R"(SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#)" + this->name + "\"\r\n";

        return headers;
      }

      std::string body() const
      {
        std::string data = R"(<?xml version="1.0" encoding="utf-8" standalone="yes"?>)";
        data += R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)";
        data += R"(<s:Body>)";
        data += this->soap_body();
        data += R"(</s:Body>)";
        data += R"(</s:Envelope>)";
        return data;
      }

    protected:
      const std::string name;

      Action(const std::string& name) : name(name) {}
      Action(const char* name) : name(name) {}

      virtual std::string soap_body() const = 0;
  };

  class SetAvTransportUriAction : public Action
  {
    public:
      std::string uri;

      SetAvTransportUriAction(const std::string& uri) : Action("SetAVTransportURI"), uri(uri) {}
      SetAvTransportUriAction(const char* uri) : Action("SetAVTransportURI"), uri(uri) {}
    
    private:
      std::string soap_body() const
      {
        std::string body = R"(<u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)";
        body += "<InstanceID>" + std::to_string(this->instanceId) + "</InstanceID>";
        body += "<CurrentURI>" + this->uri + "</CurrentURI>";
        body += "<CurrentURIMetaData></CurrentURIMetaData>";
        body += "</u:SetAVTransportURI>";
        return body;
      }
  };

  class PlayAction : public Action
  {
    public:
      int speed = 1;

      PlayAction() : Action("Play") {}

    private:
      std::string soap_body() const
      {
        std::string body = R"(<u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)";
        body += "<InstanceID>" + std::to_string(this->instanceId) + "</InstanceID>";
        body += "<Speed>" + std::to_string(this->speed) + "</Speed>";
        body += "</u:Play>";
        return body;
      }
  };

  class StopAction : public Action
  {
    public:
      StopAction() : Action("Stop") {}

    private:
      std::string soap_body() const
      {
        std::string body = R"(<u:Stop xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">)";
        body += "<InstanceID>" + std::to_string(this->instanceId) + "</InstanceID>";
        body += "</u:Stop>";
        return body;
      }
  };
}

#endif
