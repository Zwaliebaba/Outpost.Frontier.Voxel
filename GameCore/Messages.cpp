#include "Messages.h"

#include "WireFormat.h"

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
  return _kind >= static_cast<std::uint16_t>(MessageKind::Tick) && _kind <= static_cast<std::uint16_t>(MessageKind::Resource);
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
      else
      {
        static_assert(std::is_same_v<T, ResourceMessage>);
        return MessageKind::Resource;
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
    return Route{Neuron::Delivery::Datagram, STREAM_WORLD};
  case MessageKind::Ship:
  case MessageKind::Objective:
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

} // namespace Outpost
