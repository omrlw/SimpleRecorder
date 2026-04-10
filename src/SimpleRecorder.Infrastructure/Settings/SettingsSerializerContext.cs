using System.Text.Json.Serialization;
using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Infrastructure.Settings;

[JsonSerializable(typeof(AppSettings))]
internal sealed partial class SettingsSerializerContext : JsonSerializerContext;
