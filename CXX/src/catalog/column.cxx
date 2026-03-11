/**
 * CXX/src/catalog/column.cxx
 *
 * 实现 Column 的逻辑
 *
 * 包含：
 *
 * 构造函数
 * ToString
 * Validate
 */

#include "catalog/column.h"

#include <stdexcept>
#include <utility>

namespace HaruhiDB
{
namespace catalog
{

    /**
     * 内部辅助函数
     *
     * 调用 Validate
     * 如果失败直接抛异常
     */
    namespace
    {
        void ThrowIfInvalidColumnDefinition(const Column& column)
        {
            auto result = column.Validate();

            if (!result.has_value()) {
                throw std::invalid_argument(result.error());
            }
        }
    }

    /**
     * 固定长度列构造函数
     *
     * 例如：
     *
     * INT
     * DOUBLE
     */
    Column::Column(std::string name, type::TypeId type, bool nullable, std::optional<type::Value> default_value)
        : name_(std::move(name)), type_(type), nullable_(nullable), default_value_(std::move(default_value))
    {

        /**
         * 获取固定类型长度
         *
         * INT -> 4
         * DOUBLE -> 8
         */
        const int fixed_size = type::TypeUtil::FixedLengthSize(type_);

        /**
         * 如果是变长类型
         * 必须使用另一个构造函数
         */
        if (fixed_size == type::TypeUtil::VARIABLE_LENGTH) {
            throw std::invalid_argument("Column: variable-length type requires explicit length");
        }

        /**
         * 类型非法
         */
        if (fixed_size <= 0) {
            throw std::invalid_argument("Column: invalid fixed-length type");
        }

        /**
         * 设置长度
         */
        length_ = static_cast<uint32_t>(fixed_size);

        /**
         * 固定长度类型一定是 inline
         */
        inlined_ = true;

        /**
         * 验证定义是否合法
         */
        ThrowIfInvalidColumnDefinition(*this);
    }

    /**
     * 变长类型构造函数
     *
     * 用于 VARCHAR
     */
    Column::Column(
        std::string name, type::TypeId type, uint32_t length, bool nullable, std::optional<type::Value> default_value)
        : name_(std::move(name)), type_(type), length_(length), nullable_(nullable), default_value_(std::move(default_value))
    {

        /**
         * 判断是否为变长类型
         */
        inlined_ = !type::TypeUtil::IsVariableLength(type_);

        /**
         * 如果是固定长度类型
         */
        if (inlined_) {

            const int fixed_size = type::TypeUtil::FixedLengthSize(type_);

            if (fixed_size <= 0) {
                throw std::invalid_argument("Column: invalid fixed-length type");
            }

            /**
             * 长度必须匹配
             */
            if (length_ != static_cast<uint32_t>(fixed_size)) {
                throw std::invalid_argument("Column: fixed-length column size mismatch");
            }

        } else if (length_ == 0) {

            /**
             * 变长列必须指定最大长度
             */
            throw std::invalid_argument("Column: variable-length column size must be positive");
        }

        ThrowIfInvalidColumnDefinition(*this);
    }

    /**
     * 将 Column 转换为 SQL 风格字符串
     *
     * 例如：
     *
     * id INT NOT NULL
     * name VARCHAR(32) NULL
     */
    std::string Column::ToString() const
    {
        std::string output = name_ + " " + std::string(type::TypeUtil::TypeName(type_));

        if (type_ == type::TypeId::VARCHAR) {
            output += "(" + std::to_string(length_) + ")";
        }

        output += nullable_ ? " NULL" : " NOT NULL";

        if (default_value_.has_value()) {
            output += " DEFAULT ";
            output += default_value_->ToString();
        }

        return output;
    }

    /**
     * 验证 Column 定义是否合法
     *
     * 检查：
     *
     * 1 name 非空
     * 2 type 合法
     * 3 length 合法
     * 4 inlined 与 type 匹配
     * 5 default value 类型合法
     */
    std::expected<void, std::string> Column::Validate() const
    {

        /**
         * 列名不能为空
         */
        if (name_.empty()) {
            return std::unexpected("Column: name must not be empty");
        }

        /**
         * 类型必须合法
         */
        if (!type::TypeUtil::IsValid(type_) || type_ == type::TypeId::INVALID) {
            return std::unexpected("Column: invalid type id");
        }

        /**
         * 检查 inlined 是否正确
         */
        const bool should_be_inlined = !type::TypeUtil::IsVariableLength(type_);

        if (should_be_inlined != inlined_) {
            return std::unexpected("Column: inlined flag does not match type");
        }

        /**
         * 固定长度类型验证
         */
        if (inlined_) {

            const int fixed_size = type::TypeUtil::FixedLengthSize(type_);

            if (fixed_size <= 0) {
                return std::unexpected("Column: invalid fixed-length type");
            }

            if (length_ != static_cast<uint32_t>(fixed_size)) {
                return std::unexpected("Column: fixed-length column size mismatch");
            }

        } else if (length_ == 0) {

            /**
             * 变长列必须指定长度
             */
            return std::unexpected("Column: variable-length column size must be positive");
        }

        /**
         * 如果没有默认值
         * 直接合法
         */
        if (!default_value_.has_value()) {
            return {};
        }

        const type::Value& default_value = default_value_.value();

        /**
         * NULL 默认值检查
         */
        if (default_value.IsNull()) {

            if (!nullable_) {
                return std::unexpected("Column: NULL default value is not allowed for NOT NULL column");
            }

            return {};
        }

        /**
         * 默认值类型必须可转换
         */
        if (!default_value.CanCastTo(type_)) {
            return std::unexpected("Column: default value cannot cast to column type");
        }

        /**
         * VARCHAR 长度检查
         */
        if (type_ == type::TypeId::VARCHAR) {

            const auto serialized = default_value.Serialize(type_);

            if (serialized.size() > length_) {
                return std::unexpected("Column: default VARCHAR value exceeds column length");
            }
        }

        return {};
    }

} // namespace catalog
} // namespace HaruhiDB