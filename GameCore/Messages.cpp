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
  const WorldObject& object = _message.object;
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
  return _kind >= static_cast<std::uint16_t>(MessageKind::Tick) && _kind <= static_cast<std::uint16_t>(MessageKind::ObjectState);
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
      else
      {
        static_assert(std::is_same_v<T, ObjectStateMessage>);
        return MessageKind::ObjectState;
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
    return Route{Neuron::Delivery::Datagram, STREAM_WORLD};
  case MessageKind::Ship:
  case MessageKind::Objective:
  case MessageKind::ObjectSpawn:
  default:
    return Route{Neuron::Delivery::Reliable, STREAM_WORLD};
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

void ApplyMessage(const Message& _message, WorldSnapshot& _world)
{
  std::visit(
    [&_world](const auto& _value)
    {
      using T = std::decay_t<decltype(_value)>;
      if constexpr (std::is_same_v<T, TickMessage>)
      {
        _world.tick = std::max(_world.tick, _value.tick);
      }
      else if constexpr (std::is_same_v<T, ShipMessage>)
      {
        for (ShipState& ship : _world.ships)
        {
          if (ship.object == _value.ship.object)
          {
            ship = _value.ship;
            return;
          }
        }
        _world.ships.push_back(_value.ship);
      }
      else if constexpr (std::is_same_v<T, ObjectiveMessage>)
      {
        if (_world.objectives.size() <= _value.index)
        {
          _world.objectives.resize(std::size_t{_value.index} + 1);
        }
        _world.objectives[_value.index] = _value.objective;
      }
      else if constexpr (std::is_same_v<T, ResourceMessage>)
      {
        if (_world.resources.size() <= _value.index)
        {
          _world.resources.resize(std::size_t{_value.index} + 1);
          _world.resourceTicks.resize(std::size_t{_value.index} + 1, 0);
        }
        if (_value.tick >= _world.resourceTicks[_value.index])
        {
          _world.resources[_value.index] = _value.resource;
          _world.resourceTicks[_value.index] = _value.tick;
        }
      }
      else if constexpr (std::is_same_v<T, ObjectSpawnMessage>)
      {
        if (WorldObject* known = _world.FindObject(_value.object.id))
        {
          *known = _value.object;
        }
        else
        {
          _world.objects.push_back(_value.object);
        }
        ++_world.objectsRevision;
      }
      else
      {
        static_assert(std::is_same_v<T, ObjectStateMessage>);
        WorldObject* object = _world.FindObject(_value.id);
        if (object != nullptr && _value.tick >= object->poseTick)
        {
          if (!(object->pose == _value.pose))
          {
            object->pose = _value.pose;
            ++_world.objectsRevision;
          }
          object->poseTick = _value.tick;
        }
      }
    },
    _message);
}

} // namespace Outpost
