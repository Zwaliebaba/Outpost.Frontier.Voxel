#include "DockingPoint.h"

#include "SceneDefinition.h"

using namespace DirectX;

namespace Outpost
{

namespace
{

[[nodiscard]] UniversePosition Origin(const XMMATRIX& _frame) noexcept
{
  XMFLOAT3 origin{};
  XMStoreFloat3(&origin, _frame.r[3]);
  return UniversePosition{origin.x, origin.y, origin.z};
}

} // namespace

std::string_view DockKindName(DockKind _kind) noexcept
{
  switch (_kind)
  {
  case DockKind::Medium:
    return "medium";
  case DockKind::Large:
    return "large";
  case DockKind::Berth:
    break;
  }
  return "berth";
}

std::string_view DockPhaseName(DockPhase _phase) noexcept
{
  switch (_phase)
  {
  case DockPhase::FarApproach:
    return "approaching";
  case DockPhase::Waiting:
    return "waiting";
  case DockPhase::Staging:
    return "staging";
  case DockPhase::Final:
    return "docking";
  case DockPhase::Docked:
    return "docked";
  case DockPhase::Launching:
    return "launching";
  case DockPhase::None:
    break;
  }
  return "free";
}

std::string_view DockAnswerName(DockAnswer _answer) noexcept
{
  switch (_answer)
  {
  case DockAnswer::Proceed:
    return "proceed";
  case DockAnswer::Wait:
    return "wait";
  case DockAnswer::CannotDock:
    return "cannot dock";
  case DockAnswer::NoDock:
    return "no dock";
  case DockAnswer::OutOfRange:
    return "out of range";
  case DockAnswer::TimedOut:
    return "timed out";
  case DockAnswer::Canceled:
    return "canceled";
  case DockAnswer::LaunchRefused:
    return "launch refused";
  case DockAnswer::None:
    break;
  }
  return "none";
}

std::optional<DockKind> ParseDockKind(std::string_view _text) noexcept
{
  for (const DockKind kind : {DockKind::Berth, DockKind::Medium, DockKind::Large})
  {
    if (_text == DockKindName(kind))
    {
      return kind;
    }
  }
  return std::nullopt;
}

DockingPath PathFor(const SceneDefinition& _scene, std::size_t _instanceIndex, const DockingPoint& _point) noexcept
{
  DockingPath path;
  path.radiusMetres = _point.radiusMetres;
  const ModelAsset* model = _scene.FindModel(_scene.instances[_instanceIndex].model);
  if (model == nullptr)
  {
    return path;
  }
  const auto frame = [&](std::string_view _socket, UniversePosition& _out)
  {
    const Neuron::Socket* socket = model->FindSocket(_socket);
    if (socket == nullptr)
    {
      return XMMatrixIdentity();
    }
    const XMMATRIX world = SocketWorld(_scene, _instanceIndex, *socket);
    _out = Origin(world);
    return world;
  };
  frame(_point.entry, path.entry);
  frame(_point.approach, path.approach);
  const XMMATRIX pad = frame(_point.pad, path.pad);
  // The rows are the socket's right, up and outward normal: the docked ship's own X, Y and Z (its
  // nose), less any scale the instance carries.
  XMMATRIX rotation = XMMatrixIdentity();
  rotation.r[0] = XMVector3Normalize(pad.r[0]);
  rotation.r[1] = XMVector3Normalize(pad.r[1]);
  rotation.r[2] = XMVector3Normalize(pad.r[2]);
  XMStoreFloat4(&path.orientation, XMQuaternionNormalize(XMQuaternionRotationMatrix(rotation)));
  return path;
}

} // namespace Outpost
