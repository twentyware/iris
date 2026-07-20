cask "iris" do
  version "@VERSION@"
  sha256 "@SHA256_MACOS_ZIP@"

  url "https://github.com/twentyware/iris/releases/download/v#{version}/iris-macos-x64.zip"
  name "Iris"
  desc "20-20-20 eye-rest reminder that gently dims the screen every 20 minutes"
  homepage "https://github.com/twentyware/iris"

  livecheck do
    url :url
    strategy :github_latest
  end

  app "Iris.app"

  zap trash: [
    "~/Library/LaunchAgents/com.twentyware.iris.plist",
    "~/Library/Preferences/com.twentyware.iris.plist",
  ]
end
