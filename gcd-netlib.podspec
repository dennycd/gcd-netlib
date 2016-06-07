#
# Be sure to run `pod lib lint gcd-netlib.podspec' to ensure this is a
# valid spec before submitting.
#
# Any lines starting with a # are optional, but their use is encouraged
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html
#

Pod::Spec.new do |s|
  s.name             = 'gcd-netlib'
  s.version          = '0.1.0'
  s.summary          = 'A C++ Networking Library Built on Libdispatch GCD'

# This description is used to generate tags and improve search results.
#   * Think: What does it do? Why did you write it? What is the focus?
#   * Try to keep it short, snappy and to the point.
#   * Write the description between the DESC delimiters below.
#   * Finally, don't worry about the indent, CocoaPods strips it!

  s.description      = <<-DESC
A TCP socket networking library built on top of Apple's grand central dispatch (libdispatch).
                       DESC

  s.homepage         = 'https://github.com/dennycd/gcd-netlib'
  # s.screenshots     = 'www.example.com/screenshots_1', 'www.example.com/screenshots_2'
  s.license          = { :type => 'MIT', :file => 'LICENSE' }
  s.author           = { 'Denny C. Dai' => 'dennycd@me.com' }
  s.source           = { :git => 'https://github.com/dennycd/gcd-netlib.git', :tag => s.version.to_s }
  # s.social_media_url = 'https://twitter.com/<TWITTER_USERNAME>'

  s.ios.deployment_target = '8.0'

  s.source_files = 'gcd-netlib/Classes/**/*'

  # s.resource_bundles = {
  #   'gcd-netlib' => ['gcd-netlib/Assets/*.png']
  # }

  # s.public_header_files = 'Pod/Classes/**/*.h'
  # s.frameworks = 'UIKit', 'MapKit'
  # s.dependency 'AFNetworking', '~> 2.3'
end
