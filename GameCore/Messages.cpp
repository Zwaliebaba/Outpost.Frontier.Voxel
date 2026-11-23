#include "Messages.h"

#include "WireFormat.h"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace Outpost
{

namespace
{

void WritePayload(Neuron::WireWriter& _writer, const TickMessage& _message)
{
  _writer.Uint64(_message.tick);
}

void WritePayload(Neuron::WireWriter& _writer, const ShipMessage& _message)
{
  const ShipState& ship = _message.ship;
  _writer.Uint32(ship.object);
  _writer.String(ship.instance);
  _writer.String(ship.callsign);
  _writer.String(ship.shipClass);
  _writer.Int32(ship.hullPercent);
  if (ship.modules.size() > std::numeric_limits<std::uint16_t>::max())
  {
    _writer.MarkFailed();
    return;
  }
  _writer.Uint16(static_cast<std::uint16_t>(ship.modules.size()));
  for (const ShipModuleState& module : ship.modules)
  {
    _writer.String(module.name);
    _writer.Int32(module.healthPercent);
  }
}

void WritePayload(Neuron::WireWriter& _writer, const ObjectiveMessage& _message)
{
  _writer.Uint16(_message.index);
  _writer.String(_message.objective.text);
  _writer.Uint8(_message.objective.complete ? 1 : 0);
}

void WritePayload(Neuron::WireWriter& _writer, const ResourceMessage& _message)
{
  _writer.Uint64(_message.tick);
  _writer.Uint16(_message.index);
  _writer.String(_message.resource.name);
  _writer.Int64(_message.resource.amount);
  _writer.Int64(_message.resource.capacity);
}

void WritePose(Neuron::WireWriter& _writer, const Pose& _pose)
{
  _writer.Float64(_pose.position.x);
  _writer.Float64(_pose.position.y);
  _writer.Float64(_pose.position.z);
  _writer.Float32(_pose.orientation.x);
  _writer.Float32(_pose.orientation.y);
  _writer.Float32(_pose.orientation.z);
  _writer.Float32(_pose.orientation.w);
}

[[nodiscard]] Pose ReadPose(Neuron::WireReader& _reader) noexcept
{
  Pose pose;
  pose.position.x = _reader.Float64();
  pose.position.y = _reader.Float64();
  pose.position.z = _reader.Float64();
  pose.orientation.x = _reader.Float32();
  pose.orientation.y = _reader.Float32();
  pose.orientation.z = _reader.Float32();
  pose.orientation.w = _reader.Float32();
  return pose;
}

void WritePayload(Neuron::WireWriter& _writer, const ObjectSpawnMessage& _message)
{
  const UniverseObject& object = _message.object;
  _writer.Uint32(object.id);
  _writer.String(object.name);
  _writer.String(object.model);
  _writer.Uint8(static_cast<std::uint8_t>(object.owner));
  _writer.Float32(object.scale);
  _writer.Uint64(object.poseTick);
  WritePose(_writer, object.pose);
}

void WritePayload(Neuron::WireWriter& _writer, const ObjectStateMessage& _message)
{
  _writer.Uint64(_message.tick);
  _writer.Uint32(_message.id);
  WritePose(_writer, _message.pose);
}

[[nodiscard]] bool ReadBool(Neuron::WireReader& _reader, bool& _out) noexcept
{
  const std::uint8_t value = _reader.Uint8();
  _out = value == 1;
  return value <= 1;
}

void WriteFloat3(Neuron::WireWriter& _writer, const DirectX::XMFLOAT3& _value)
{
  _writer.Float32(_value.x);
  _writer.Float32(_value.y);
  _writer.Float32(_value.z);
}

[[nodiscard]] DirectX::XMFLOAT3 ReadFloat3(Neuron::WireReader& _reader) noexcept
{
  DirectX::XMFLOAT3 value{};
  value.x = _reader.Float32();
  value.y = _reader.Float32();
  value.z = _reader.Float32();
  return value;
}

void WritePosition(Neuron::WireWriter& _writer, const UniversePosition& _position)
{
  _writer.Float64(_position.x);
  _writer.Float64(_position.y);
  _writer.Float64(_position.z);
}

[[nodiscard]] UniversePosition ReadPosition(Neuron::WireReader& _reader) noexcept
{
  UniversePosition position;
  position.x = _reader.Float64();
  position.y = _reader.Float64();
  position.z = _reader.Float64();
  return position;
}

/// An enumeration's byte, refused above its last value.
template <typename E> [[nodiscard]] bool ReadEnum(Neuron::WireReader& _reader, E _last, E& _out) noexcept
{
  const std::uint8_t value = _reader.Uint8();
  _out = static_cast<E>(value);
  return value <= static_cast<std::uint8_t>(_last);
}

// The switches of FlightControls, one bit each.
constexpr std::uint8_t CONTROL_REVERSE = 1;
constexpr std::uint8_t CONTROL_THRUSTER = 2;
constexpr std::uint8_t CONTROL_ENGINE_ON = 4;
constexpr std::uint8_t CONTROL_CRUISE = 8;
constexpr std::uint8_t CONTROL_FIRE = 16;
constexpr std::uint8_t CONTROL_ALL = 31;

void WritePayload(Neuron::WireWriter& _writer, const FlightInputMessage& _message)
{
  const ShipCommand& command = _message.command;
  _writer.Uint64(_message.tick);
  _writer.Uint32(_message.ship);
  _writer.Uint8(static_cast<std::uint8_t>(command.manoeuvre.kind));
  WritePosition(_writer, command.manoeuvre.target);
  _writer.Float32(command.manoeuvre.standOffMetres);
  const FlightControls& wishes = command.wishes;
  _writer.Float32(wishes.throttle);
  WriteFloat3(_writer, wishes.steering);
  _writer.Uint8(static_cast<std::uint8_t>(wishes.strafe));
  _writer.Uint8(static_cast<std::uint8_t>((wishes.reverse ? CONTROL_REVERSE : 0) | (wishes.thruster ? CONTROL_THRUSTER : 0) |
                                          (wishes.engineOn ? CONTROL_ENGINE_ON : 0) | (wishes.cruise ? CONTROL_CRUISE : 0) |
                                          (wishes.fire ? CONTROL_FIRE : 0)));
  WritePosition(_writer, command.aim);
}

void WritePayload(Neuron::WireWriter& _writer, const OwnShipMessage& _message)
{
  const FlightState& state = _message.state;
  _writer.Uint64(_message.tick);
  _writer.Uint32(_message.ship);
  WritePosition(_writer, state.position);
  _writer.Float32(state.orientation.x);
  _writer.Float32(state.orientation.y);
  _writer.Float32(state.orientation.z);
  _writer.Float32(state.orientation.w);
  WriteFloat3(_writer, state.velocityMetresPerSecond);
  WriteFloat3(_writer, state.angularVelocityRadiansPerSecond);
  _writer.Float32(state.powerPool);
  _writer.Float32(state.thrustPool);
  _writer.Uint8(state.thrusterActive ? 1 : 0);
  _writer.Uint8(static_cast<std::uint8_t>(state.cruise));
  _writer.Int32(state.cruiseTimerTicks);
  _writer.Float32(state.cruiseRamp);
  _writer.Float32(state.shield);
  _writer.Uint8(state.shieldUp ? 1 : 0);
  _writer.Int32(state.shieldRebuildTicks);
  for (const std::int32_t clock : state.launcherClockTicks)
  {
    _writer.Int32(clock);
  }
}

void WritePayload(Neuron::WireWriter& _writer, const ShipMotionMessage& _message)
{
  const ShipMotionState& motion = _message.motion;
  _writer.Uint32(motion.object);
  _writer.Uint64(motion.tick);
  _writer.Float32(motion.throttle);
  _writer.Float32(motion.speedMetresPerSecond);
  _writer.Uint8(motion.thruster ? 1 : 0);
  _writer.Uint8(motion.engineOn ? 1 : 0);
  _writer.Uint8(static_cast<std::uint8_t>(motion.cruise));
}

void WritePayload(Neuron::WireWriter& _writer, const DockCommandMessage& _message)
{
  _writer.Uint64(_message.tick);
  _writer.Uint32(_message.ship);
  _writer.Uint8(static_cast<std::uint8_t>(_message.command));
}

void WritePayload(Neuron::WireWriter& _writer, const ObjectRemovedMessage& _message)
{
  _writer.Uint64(_message.tick);
  _writer.Uint32(_message.id);
}

void WritePayload(Neuron::WireWriter& _writer, const UseItemMessage& _message)
{
  _writer.Uint64(_message.tick);
  _writer.Uint32(_message.ship);
  _writer.Uint8(static_cast<std::uint8_t>(_message.item));
}

void WritePayload(Neuron::WireWriter& _writer, const DockStatusMessage& _message)
{
  const DockStatusState& status = _message.status;
  _writer.Uint32(_message.ship);
  _writer.Uint64(status.tick);
  _writer.Uint8(static_cast<std::uint8_t>(status.phase));
  _writer.Uint8(static_cast<std::uint8_t>(status.answer));
  _writer.String(status.station);
  _writer.String(status.reason);
}

[[nodiscard]] std::expected<Message, DecodeError> ReadPayload(MessageKind _kind, Neuron::WireReader& _reader)
{
  bool wellFormed = true;
  Message message;
  switch (_kind)
  {
  case MessageKind::Tick:
    message = TickMessage{_reader.Uint64()};
    break;
  case MessageKind::Ship:
  {
    ShipMessage ship;
    ship.ship.object = _reader.Uint32();
    ship.ship.instance = _reader.String();
    ship.ship.callsign = _reader.String();
    ship.ship.shipClass = _reader.String();
    ship.ship.hullPercent = _reader.Int32();
    const std::uint16_t moduleCount = _reader.Uint16();
    for (std::uint16_t i = 0; i < moduleCount && _reader.Ok(); ++i)
    {
      ShipModuleState module;
      module.name = _reader.String();
      module.healthPercent = _reader.Int32();
      ship.ship.modules.push_back(std::move(module));
    }
    message = std::move(ship);
    break;
  }
  case MessageKind::Objective:
  {
    ObjectiveMessage objective;
    objective.index = _reader.Uint16();
    objective.objective.text = _reader.String();
    wellFormed = ReadBool(_reader, objective.objective.complete);
    message = std::move(objective);
    break;
  }
  case MessageKind::Resource:
  {
    ResourceMessage resource;
    resource.tick = _reader.Uint64();
    resource.index = _reader.Uint16();
    resource.resource.name = _reader.String();
    resource.resource.amount = _reader.Int64();
    resource.resource.capacity = _reader.Int64();
    message = std::move(resource);
    break;
  }
  case MessageKind::ObjectSpawn:
  {
    ObjectSpawnMessage spawn;
    spawn.object.id = _reader.Uint32();
    spawn.object.name = _reader.String();
    spawn.object.model = _reader.String();
    const std::uint8_t owner = _reader.Uint8();
    wellFormed = owner <= static_cast<std::uint8_t>(Owner::Hostile);
    spawn.object.owner = static_cast<Owner>(owner);
    spawn.object.scale = _reader.Float32();
    spawn.object.poseTick = _reader.Uint64();
    spawn.object.pose = ReadPose(_reader);
    message = std::move(spawn);
    break;
  }
  case MessageKind::ObjectState:
  {
    ObjectStateMessage state;
    state.tick = _reader.Uint64();
    state.id = _reader.Uint32();
    state.pose = ReadPose(_reader);
    message = state;
    break;
  }
  case MessageKind::FlightInput:
  {
    FlightInputMessage input;
    input.tick = _reader.Uint64();
    input.ship = _reader.Uint32();
    Manoeuvre& manoeuvre = input.command.manoeuvre;
    wellFormed = ReadEnum(_reader, ManoeuvreKind::GoTo, manoeuvre.kind);
    manoeuvre.target = ReadPosition(_reader);
    manoeuvre.standOffMetres = _reader.Float32();
    FlightControls& wishes = input.command.wishes;
    wishes.throttle = _reader.Float32();
    wishes.steering = ReadFloat3(_reader);
    wellFormed = ReadEnum(_reader, StrafeDirection::Down, wishes.strafe) && wellFormed;
    const std::uint8_t switches = _reader.Uint8();
    wellFormed = wellFormed && switches <= CONTROL_ALL;
    wishes.reverse = (switches & CONTROL_REVERSE) != 0;
    wishes.thruster = (switches & CONTROL_THRUSTER) != 0;
    wishes.engineOn = (switches & CONTROL_ENGINE_ON) != 0;
    wishes.cruise = (switches & CONTROL_CRUISE) != 0;
    wishes.fire = (switches & CONTROL_FIRE) != 0;
    input.command.aim = ReadPosition(_reader);
    message = input;
    break;
  }
  case MessageKind::OwnShip:
  {
    OwnShipMessage own;
    own.tick = _reader.Uint64();
    own.ship = _reader.Uint32();
    FlightState& state = own.state;
    state.position = ReadPosition(_reader);
    state.orientation.x = _reader.Float32();
    state.orientation.y = _reader.Float32();
    state.orientation.z = _reader.Float32();
    state.orientation.w = _reader.Float32();
    state.velocityMetresPerSecond = ReadFloat3(_reader);
    state.angularVelocityRadiansPerSecond = ReadFloat3(_reader);
    state.powerPool = _reader.Float32();
    state.thrustPool = _reader.Float32();
    wellFormed = ReadBool(_reader, state.thrusterActive);
    wellFormed = ReadEnum(_reader, CruiseState::Cooldown, state.cruise) && wellFormed;
    state.cruiseTimerTicks = _reader.Int32();
    state.cruiseRamp = _reader.Float32();
    state.shield = _reader.Float32();
    wellFormed = ReadBool(_reader, state.shieldUp) && wellFormed;
    state.shieldRebuildTicks = _reader.Int32();
    for (std::int32_t& clock : state.launcherClockTicks)
    {
      clock = _reader.Int32();
    }
    message = own;
    break;
  }
  case MessageKind::ShipMotion:
  {
    ShipMotionMessage motion;
    motion.motion.object = _reader.Uint32();
    motion.motion.tick = _reader.Uint64();
    motion.motion.throttle = _reader.Float32();
    motion.motion.speedMetresPerSecond = _reader.Float32();
    wellFormed = ReadBool(_reader, motion.motion.thruster);
    wellFormed = ReadBool(_reader, motion.motion.engineOn) && wellFormed;
    wellFormed = ReadEnum(_reader, CruiseState::Cooldown, motion.motion.cruise) && wellFormed;
    message = motion;
    break;
  }
  case MessageKind::DockCommand:
  {
    DockCommandMessage command;
    command.tick = _reader.Uint64();
    command.ship = _reader.Uint32();
    wellFormed = ReadEnum(_reader, DockCommand::Launch, command.command);
    message = command;
    break;
  }
  case MessageKind::DockStatus:
  {
    DockStatusMessage status;
    status.ship = _reader.Uint32();
    status.status.tick = _reader.Uint64();
    wellFormed = ReadEnum(_reader, DockPhase::Launching, status.status.phase);
    wellFormed = ReadEnum(_reader, DockAnswer::LaunchRefused, status.status.answer) && wellFormed;
    status.status.station = _reader.String();
    status.status.reason = _reader.String();
    message = status;
    break;
  }
  case MessageKind::ObjectRemoved:
  {
    ObjectRemovedMessage removed;
    removed.tick = _reader.Uint64();
    removed.id = _reader.Uint32();
    message = removed;
    break;
  }
  case MessageKind::UseItem:
  {
    UseItemMessage use;
    use.tick = _reader.Uint64();
    use.ship = _reader.Uint32();
    wellFormed = ReadEnum(_reader, ItemKind::RepairKit, use.item);
    message = use;
    break;
  }
  default:
    return std::unexpected(DecodeError::UnknownKind);
  }
  if (!_reader.Ok() || !wellFormed || _reader.Remaining() != 0)
  {
    return std::unexpected(DecodeError::Malformed);
  }
  return message;
}

[[nodiscard]] bool IsKnownKind(std::uint16_t _kind) noexcept
{
  return _kind >= static_cast<std::uint16_t>(MessageKind::Tick) && _kind <= static_cast<std::uint16_t>(MessageKind::UseItem);
}

} // namespace

MessageKind KindOf(const Message& _message)
{
  return std::visit(
    [](const auto& _value)
    {
      using T = std::decay_t<decltype(_value)>;
      if constexpr (std::is_same_v<T, TickMessage>)
      {
        return MessageKind::Tick;
      }
      else if constexpr (std::is_same_v<T, ShipMessage>)
      {
        return MessageKind::Ship;
      }
      else if constexpr (std::is_same_v<T, ObjectiveMessage>)
      {
        return MessageKind::Objective;
      }
      else if constexpr (std::is_same_v<T, ResourceMessage>)
      {
        return MessageKind::Resource;
      }
      else if constexpr (std::is_same_v<T, ObjectSpawnMessage>)
      {
        return MessageKind::ObjectSpawn;
      }
      else if constexpr (std::is_same_v<T, ObjectStateMessage>)
      {
        return MessageKind::ObjectState;
      }
      else if constexpr (std::is_same_v<T, FlightInputMessage>)
      {
        return MessageKind::FlightInput;
      }
      else if constexpr (std::is_same_v<T, OwnShipMessage>)
      {
        return MessageKind::OwnShip;
      }
      else if constexpr (std::is_same_v<T, ShipMotionMessage>)
      {
        return MessageKind::ShipMotion;
      }
      else if constexpr (std::is_same_v<T, DockCommandMessage>)
      {
        return MessageKind::DockCommand;
      }
      else if constexpr (std::is_same_v<T, DockStatusMessage>)
      {
        return MessageKind::DockStatus;
      }
      else if constexpr (std::is_same_v<T, ObjectRemovedMessage>)
      {
        return MessageKind::ObjectRemoved;
      }
      else
      {
        static_assert(std::is_same_v<T, UseItemMessage>);
        return MessageKind::UseItem;
      }
    },
    _message);
}

Route RouteOf(MessageKind _kind) noexcept
{
  switch (_kind)
  {
  case MessageKind::Tick:
  case MessageKind::Resource:
  case MessageKind::ObjectState:
  case MessageKind::ShipMotion:
    return Route{Neuron::Delivery::Datagram, STREAM_UNIVERSE};
  case MessageKind::FlightInput:
    return Route{Neuron::Delivery::Datagram, STREAM_COMMANDS};
  case MessageKind::DockCommand:
  case MessageKind::UseItem:
    return Route{Neuron::Delivery::Reliable, STREAM_COMMANDS};
  case MessageKind::Ship:
  case MessageKind::Objective:
  case MessageKind::ObjectSpawn:
  default:
    return Route{Neuron::Delivery::Reliable, STREAM_UNIVERSE};
  }
}

std::vector<std::uint8_t> Encode(const Message& _message)
{
  Neuron::WireWriter payload;
  std::visit([&payload](const auto& _value) { WritePayload(payload, _value); }, _message);
  if (!payload.Ok() || payload.Bytes().size() > std::numeric_limits<std::uint32_t>::max())
  {
    return {};
  }
  Neuron::WireWriter framed;
  framed.Uint16(CATALOGUE_VERSION);
  framed.Uint16(static_cast<std::uint16_t>(KindOf(_message)));
  framed.Uint32(static_cast<std::uint32_t>(payload.Bytes().size()));
  std::vector<std::uint8_t> bytes = framed.TakeBytes();
  bytes.insert(bytes.end(), payload.Bytes().begin(), payload.Bytes().end());
  return bytes;
}

std::expected<Message, DecodeError> Decode(std::span<const std::uint8_t> _bytes)
{
  if (_bytes.size() < MESSAGE_HEADER_BYTES)
  {
    return std::unexpected(DecodeError::Truncated);
  }
  Neuron::WireReader header(_bytes.first(MESSAGE_HEADER_BYTES));
  const std::uint16_t version = header.Uint16();
  const std::uint16_t kind = header.Uint16();
  const std::uint32_t length = header.Uint32();
  if (version != CATALOGUE_VERSION)
  {
    return std::unexpected(DecodeError::WrongVersion);
  }
  if (!IsKnownKind(kind))
  {
    return std::unexpected(DecodeError::UnknownKind);
  }
  const std::size_t available = _bytes.size() - MESSAGE_HEADER_BYTES;
  if (available < length)
  {
    return std::unexpected(DecodeError::Truncated);
  }
  if (available > length)
  {
    return std::unexpected(DecodeError::LengthMismatch);
  }
  Neuron::WireReader payload(_bytes.subspan(MESSAGE_HEADER_BYTES));
  return ReadPayload(static_cast<MessageKind>(kind), payload);
}

void ApplyMessage(const Message& _message, UniverseSnapshot& _universe)
{
  std::visit(
    [&_universe](const auto& _value)
    {
      using T = std::decay_t<decltype(_value)>;
      if constexpr (std::is_same_v<T, TickMessage>)
      {
        _universe.tick = std::max(_universe.tick, _value.tick);
      }
      else if constexpr (std::is_same_v<T, ShipMessage>)
      {
        for (ShipState& ship : _universe.ships)
        {
          if (ship.object == _value.ship.object)
          {
            ship = _value.ship;
            return;
          }
        }
        _universe.ships.push_back(_value.ship);
      }
      else if constexpr (std::is_same_v<T, ObjectiveMessage>)
      {
        if (_universe.objectives.size() <= _value.index)
        {
          _universe.objectives.resize(std::size_t{_value.index} + 1);
        }
        _universe.objectives[_value.index] = _value.objective;
      }
      else if constexpr (std::is_same_v<T, ResourceMessage>)
      {
        if (_universe.resources.size() <= _value.index)
        {
          _universe.resources.resize(std::size_t{_value.index} + 1);
          _universe.resourceTicks.resize(std::size_t{_value.index} + 1, 0);
        }
        if (_value.tick >= _universe.resourceTicks[_value.index])
        {
          _universe.resources[_value.index] = _value.resource;
          _universe.resourceTicks[_value.index] = _value.tick;
        }
      }
      else if constexpr (std::is_same_v<T, ObjectSpawnMessage>)
      {
        if (UniverseObject* known = _universe.FindObject(_value.object.id))
        {
          *known = _value.object;
        }
        else
        {
          _universe.objects.push_back(_value.object);
        }
        ++_universe.objectsRevision;
      }
      else if constexpr (std::is_same_v<T, ObjectStateMessage>)
      {
        UniverseObject* object = _universe.FindObject(_value.id);
        if (object != nullptr && _value.tick >= object->poseTick)
        {
          if (!(object->pose == _value.pose))
          {
            object->pose = _value.pose;
            ++_universe.posesRevision;
          }
          object->poseTick = _value.tick;
        }
      }
      else if constexpr (std::is_same_v<T, OwnShipMessage>)
      {
        _universe.ownShip = _value.ship;
        _universe.ownShipTick = _value.tick;
        _universe.ownShipState = _value.state;
        ++_universe.ownShipRevision;
      }
      else if constexpr (std::is_same_v<T, ShipMotionMessage>)
      {
        for (ShipMotionState& motion : _universe.motions)
        {
          if (motion.object == _value.motion.object)
          {
            if (_value.motion.tick >= motion.tick)
            {
              motion = _value.motion;
            }
            return;
          }
        }
        _universe.motions.push_back(_value.motion);
      }
      else if constexpr (std::is_same_v<T, DockStatusMessage>)
      {
        _universe.ownDock = _value.status;
        ++_universe.ownDockRevision;
      }
      else if constexpr (std::is_same_v<T, ObjectRemovedMessage>)
      {
        const auto object = std::ranges::find(_universe.objects, _value.id, &UniverseObject::id);
        if (object == _universe.objects.end())
        {
          return;
        }
        _universe.objects.erase(object);
        std::erase_if(_universe.ships, [&](const ShipState& _ship) { return _ship.object == _value.id; });
        std::erase_if(_universe.motions, [&](const ShipMotionState& _motion) { return _motion.object == _value.id; });
        ++_universe.objectsRevision;
      }
      else
      {
        // A command is the server's to act on; a client's picture of the universe has no place for one.
        static_assert(std::is_same_v<T, FlightInputMessage> || std::is_same_v<T, DockCommandMessage> || std::is_same_v<T, UseItemMessage>);
      }
    },
    _message);
}

} // namespace Outpost
